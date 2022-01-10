/** 
 * section: 	XPath
 * synopsis: 	Evaluate XPath expression and prints result node set.
 * purpose: 	Shows how to evaluate XPath expression and register 
 *          	known namespaces in XPath context.
 * usage:	xpath1 <xml-file> <xpath-expr> [<known-ns-list>]
 * test:	xpath1 test3.xml '//child2' > xpath1.tmp && diff xpath1.tmp $(srcdir)/xpath1.res
 * author: 	Aleksey Sanin
 * copy: 	see Copyright for the status of this software.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>


#define THROW(x) (x); goto exception_handler;
#define CATCH(x) exception_handler: switch(x)

int verbose = 0;

extern char* optarg;
extern int optind;

const char* usage=
"%s [-v] xpath-expression filename...\n";


static char * trim_space(char *str) {
    char *end;
    /* skip leading whitespace */
    while (isspace(*str)) {
        str = str + 1;
    }
    /* remove trailing whitespace */
    end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) {
        end = end - 1;
    }
    /* write null character */
    *(end+1) = '\0';
    return str;
}

int process_xml(const char* filename, const char* xpath);
int output(const char* filename, const char* fmt, ...);

int main(int argc, char **argv) {
    int opt;

    while ((opt = getopt(argc, argv, "v"))!=-1) {
        switch (opt) {

            case 'v':
                verbose = 1;
        		break;
            default:
                fprintf(stderr, "Invalid option!\n");
                fprintf(stderr, usage, argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    int num = argc - optind;
    char** args = argv + optind;

    if (num<1) {
        fprintf(stderr, "Must specify xpath expression (and optionally filenames)\n");
        fprintf(stderr, usage, argv[0]);
        exit(EXIT_FAILURE);
    }
    char* expr = strdup(args[0]);
    
    /* Init libxml */     
    xmlInitParser();
    LIBXML_TEST_VERSION

    /* Do the main job */
    if (num>1) {
        for (int i=1; i<num; i++) {
            process_xml(args[i], args[0]);
        }        
    } else {
        process_xml("-", args[0]);
    }

    /* Shutdown libxml */
    xmlCleanupParser();
    
    /*
     * this is to debug memory for regression tests
     */
    xmlMemoryDump();
    return 0;
}

static char* readstdio(FILE* stream, size_t* result) {
    char* bytes=NULL;
    size_t size=0;
    size_t offset=0;
    size_t bytes_read;
#define READSIZE 16384
    do  {
        char* new_bytes = realloc(bytes, size + READSIZE);
        if (new_bytes) {
            size = size + READSIZE;
            bytes = new_bytes;
        } else {
            perror("read_stdio()");
            if (bytes) free(bytes);
            return NULL;
        }
        bytes_read = fread(bytes + offset, 1, READSIZE, stream);
        offset += bytes_read;
        // fprintf(stderr, "bytes_read=%ld, offset=%ld, bytes=%p, feof()=%d\n", bytes_read, offset, bytes, feof(stream));;
    } while (bytes_read>0 && feof(stream)==0);
    if (result) *result = offset;
    return bytes;
}

static char* readfile(const char* filename, size_t* result) {
    char* bytes=NULL;
    size_t size=0;
    FILE* fp;
    struct stat file_stats;
    if (result==NULL) {
        perror("readfile()");
        return NULL;
    };
    if ( (stat(filename, &file_stats))==0 && 
        (fp=fopen(filename, "r"))!=NULL ) {
        size=file_stats.st_size;
        bytes=calloc(size+1, 1);
        if ((*result=fread(bytes, 1, size, fp))<size) {
            fprintf(stderr, "Reading from file %s was truncated (expected %ld, got %ld)\n", filename, size, *result);
        }
        fclose(fp);
    } else {
        fprintf(stderr, "Couldn't open input file %s\n", filename);
        return NULL; 
    }
    return bytes;
}

int output(const char* filename, const char* fmt, ...) {
	va_list ap;
	size_t result;
	if (strcmp(filename, "-")!=0) {
		fprintf(stdout, "%s: ", filename);
	}
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
}


int process_xml(const char* filename, const char* xpath) {

    xmlDoc* doc=NULL;
    xmlNode* node=NULL;
    xmlXPathContext* ctx=NULL;
    xmlXPathObject* result=NULL;
    char* mem = NULL;
    size_t memsize = 0;
    int rc = 0;

    /* Read file into memory and kill xmlns= tags. Yes, it's a very ugly hack */
    if (strcmp(filename, "-")==0) {
        mem = readstdio(stdin, &memsize);
    } else {
        mem = readfile(filename, &memsize);
    }
    if (mem == NULL) {
        fprintf(stderr, "Can't read file into memory\n");
        THROW(rc=-1);
    }
    char* nsattrib=strstr(mem, "xmlns=");
    if (nsattrib) *nsattrib='X';

    /* Parse the content */
    doc = xmlReadMemory(mem, memsize, filename, NULL, XML_PARSE_NOBLANKS);
    if (doc == NULL ) {
        fprintf(stderr,"Document not parsed successfully. \n");
        THROW(rc=-1);
    }

    ctx = xmlXPathNewContext(doc);
    if (ctx==NULL) {
        fprintf(stderr, "Can't create XPath context\n");
        THROW(rc=-1)
    }

    result = xmlXPathEvalExpression(xpath, ctx);
    if (result==NULL) {
        fprintf(stderr, "XPath expression invalid!\n");
        THROW(rc==-1)
    }

    switch (result->type) {
        case XPATH_STRING:
            output(filename, "\'%s\'\n", trim_space(result->stringval));
            break;
        case XPATH_NUMBER:
            output(filename, "\'%f\'\n", result->floatval);
            break;
        case XPATH_BOOLEAN:
            output(filename, "\'%s\'\n", result->boolval?"true":"false");
            break;

        case XPATH_NODESET:

            /* This shouldn't really happen, but does seem to occasionally when
             * namespaces are used
             */
            if (result->nodesetval==NULL) {
                output(filename, "NULL\n");
                break;
            }

            /* Print number of values and text summary */
            // fprintf(stderr, "Node-set of %d value(s):\n", result->nodesetval->nodeNr);
            // xmlChar* text = xmlNodeListGetString(doc, *(result->nodesetval->nodeTab), 1);
            // fprintf(stderr, "Content: %s\n", text);
            // xmlFree(text);

            /* Iterate through the node-set, printing */
            output(filename, "\'[\n");

            for (int i=0; i<result->nodesetval->nodeNr; i++) {
                xmlChar* text;
                node=result->nodesetval->nodeTab[i];

                switch (node->type) {

                    case XML_ELEMENT_NODE:
                        text = xmlNodeListGetString(doc, node->xmlChildrenNode, 0);
                        output(filename, " \"%s\": \"%s\",\n",
                            node->name,
                            (text==NULL?"":trim_space((char*)text)));
                        xmlFree(text);
                        break;

                    case XML_TEXT_NODE:
                        output(filename, "\"%s\"\n", trim_space(node->content));
                        break;
                }

            }

            output(filename, "]\'\n");

            break;

    }

    CATCH(rc) {
        default:
        if (result) xmlXPathFreeObject(result);
        if (ctx) xmlXPathFreeContext(ctx);
        if (doc) xmlFreeDoc(doc);
        if (mem) free(mem);         
    }

    return rc;
}

