/*************************************************************************\
* Copyright (c) 2009 Helmholtz-Zentrum Berlin fuer Materialien und Energie.
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
*     National Laboratory.
* Copyright (c) 2002 The Regents of the University of California, as
*     Operator of Los Alamos National Laboratory.
* Copyright (c) 2002 Berliner Elektronenspeicherringgesellschaft fuer
*     Synchrotronstrahlung.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange (BESSY)
 *
 *  Modification History
 *  2008/04/16 Ralph Lange (BESSY)
 *     Updated usage info
 *  2009/03/31 Larry Hoff (BNL)
 *     Added field separators
 *  2009/04/01 Ralph Lange (HZB/BESSY)
 *     Added support for long strings (array of char) and quoting of nonprintable characters
 *
 */
//'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
// Version v02 2016-07-07 by Andrei Sukhanov. Bridge to ADO.
// Version v03 2016-07-15 by AS. parameter map in csv file. only two arguments supported: the adoname and the filename of the table
// Version v04 2016-07-15 by AS.
// Version v06 2016-07-25 by AS. Better printing.
// Version v07 2016-07-27 by AS. Checked for memory leaks, valgrind reports 20-byte leak but 10-hour run shows no memory increase.
// Version v08 2016-08-04 by AS. TODO Need cleanup
//                               TODO handle arrays, strings and other settable PVs, marked '<'

#include <stdio.h>
#include <epicsStdlib.h>
#include <string.h>

#include <cadef.h>
#include <epicsGetopt.h>

#include "tool_lib.h"

void usage (const char* progname)
{
    fprintf (stderr, "Monitor epics PVs and, if changed, update corresponding ADO parameters, defined in the csv file\n"
    "\nUsage: %s [options] ADO_name csv_file\n"
    "\n"
    "  -h:       Help; Print this message\n"
    "  -v:       Verbosity mask: 1-info, 2-debug, 4-detailed. Default: 1\n"
    "Channel Access options:\n"
    "  -w <sec>: Wait time, specifies CA timeout, default is %f second(s)\n"
    "  -m <msk>: Specify CA event mask to use.  <msk> is any combination of\n"
    "            'v' (value), 'a' (alarm), 'l' (log/archive), 'p' (property).\n"
    "            Default event mask is 'va'\n"
    "  -p <pri>: CA priority (0-%u, default 0=lowest)\n"
    "Timestamps:\n"
    "  Default:  Print absolute timestamps (as reported by CA server)\n"
    "  -t <key>: Specify timestamp source(s) and type, with <key> containing\n"
    "            's' = CA server (remote) timestamps\n"
    "            'c' = CA client (local) timestamps (shown in '()'s)\n"
    "            'n' = no timestamps\n"
    "            'r' = relative timestamps (time elapsed since start of program)\n"
    "            'i' = incremental timestamps (time elapsed since last update)\n"
    "            'I' = incremental timestamps (time since last update, by channel)\n"
    "            'r', 'i' or 'I' require 's' or 'c' to select the time source\n"
    "Enum format:\n"
    "  -n:       Print DBF_ENUM values as number (default is enum string)\n"
    "Array values: Print number of elements, then list of values\n"
    "  Default:  Request and print all elements (dynamic arrays supported)\n"
    "  -# <num>: Request and print up to <num> elements\n"
    "  -S:       Print arrays of char as a string (long string)\n"
    "Floating point format:\n"
    "  Default:  Use %%g format\n"
    "  -e <num>: Use %%e format, with a precision of <num> digits\n"
    "  -f <num>: Use %%f format, with a precision of <num> digits\n"
    "  -g <num>: Use %%g format, with a precision of <num> digits\n"
    "  -s:       Get value as string (honors server-side precision)\n"
    "  -lx:      Round to long integer and print as hex number\n"
    "  -lo:      Round to long integer and print as octal number\n"
    "  -lb:      Round to long integer and print as binary number\n"
    "Integer number format:\n"
    "  Default:  Print as decimal number\n"
    "  -0x:      Print as hex number\n"
    "  -0o:      Print as octal number\n"
    "  -0b:      Print as binary number\n"
    "Alternate output field separator:\n"
    "  -F <ofs>: Use <ofs> to separate fields in output\n"
    "\n"
    "Example: %s simple.test epics2ado_simple.csv\n"
             , progname, DEFAULT_TIMEOUT, CA_PRIORITY_MAX, progname);
}

//'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
// ADO-related stuff
//
#include <stdlib.h>

#define MAXRECORDS 100
#define MINCOLS 3 // expected minimum number of columns
#define MAXTOKENS  MAXRECORDS*MINCOLS
#define MAXTOKENSIZE 100

#define VERB_INFO 1
#define VERB_DEBUG 2
#define VERB_DETAILED 4
int gVerb = 1;

// globals
char *gpv2ado_map[MAXTOKENS]; // epics-to-ado map, filled by parse_epics2ado_csvmap
                              // 3 elements per record: 0:epics_PVname, 1:flag(direction), 2:ado_param_name
                              // the flag defines the data direction: three options: '>', '<', and 'x'
char gstorage[MAXTOKENS*MAXTOKENSIZE];
int gncols=0;
int gnPvs=0;
char *gAdoName=NULL;

// parse_epics2ado_csvmap
// read the csv file with epics-to-ado table and fill gpv2ado_map
int parse_epics2ado_csvmap(
  const char *filename,  //in:
  const int selectkey,   //in: select key for column[1]
                         // selectkey='>': select records for epics -> ado conversion
                         // selectkey='<': select records for ado -> epics conversion
                         // selectkey='x': select all records
  int *ncols,            //out: number of columns in the table
  char *tokens[],        //io: array for accepting the token pointers
  const int max_tokens,  //in: size of array of token pointers
  char *storage,         //io: storage for token strings
  const int storage_size //in: size of the storage
)
{
  #define MAX_STRING_LENGTH 100
  FILE *pFile;
  char mystring [MAX_STRING_LENGTH];
  char *instring = NULL;
  char *pch;
  int ntoks=0;
  int filled=0;
  int ii=0;
  int col=0;
  char *key = NULL;
  *ncols = 0;

  pFile = fopen (filename,"r");
  if (pFile == NULL)
  {
    sprintf(mystring,"ERROR opening file %s",filename);
    perror (mystring);
    return 1;
  }
  if(gVerb&VERB_INFO) printf("processing %s for epics %c ado records\n",filename,selectkey);
    while (fgets (mystring , MAX_STRING_LENGTH , pFile) != NULL )
    {
      ii++;
      //if(ii>10) break;
      if(gVerb&VERB_DETAILED) puts (mystring);
      if(mystring[0]=='#') continue;
      instring = mystring+1; //skip first comma

      //check if the field in the second column matches the key
      key = strchr(instring,',');
      if(key[1] != selectkey && selectkey != '*') //check if record should be dropped
        if(key[1] != 'x')
           if (!(key[1] != '.' && selectkey == 'x')) //
        {
          //printf("dropping line %i key %c, select %c\n",ii,key[1],(char)selectkey);
          continue;
        }
      if(gVerb&VERB_DEBUG) printf ("Splitting string \"%s\" into tokens:\n",instring);
      pch = strtok (instring," ,\"\n");
      col = 0;
      while (pch != NULL)
      {
        if(ntoks >= max_tokens) {printf("ERROR. too many tokens in epics2ado.csv\n"); exit(EXIT_FAILURE);}
        if(filled + strlen(pch) >= storage_size) {perror("ERROR. too little storage for epics2ado.csv\n"); exit(EXIT_FAILURE);}

        tokens[ntoks] = storage + filled;
        //printf ("<%s>\n",pch);
        strcpy(storage+filled,pch);
        filled += strlen(pch)+1;
        ntoks++;
        pch = strtok (NULL, " ,\"\n");
        col++;
      }
      if(ncols[0]==0) ncols[0] = col;
      else if(ncols[0] != col)
      {
        printf("ERROR inconsistent number of columns in the epics2ado table line %i, col %i\n",ii, col);
        exit(EXIT_FAILURE);
      }
  }
  if(ncols[0]) ntoks /= ncols[0];
  if(gVerb&VERB_INFO) printf("Number of records selected: %i\n",ntoks);
  //for(ii=0;ii<ntoks;ii++) printf("<%s>\n",tokens[ii]);

  fclose(pFile);
  return ntoks;
}
// pv2param
// return the ado parameter name for the epics PV using conversion table
char* pv2param(const char* pvname, char* table[], const int ncols, const int nrows)
{
	int ii;
	char* nothing = "";
	for (ii=0; ii<nrows; ii++)
	{
		if(strcmp(table[ii*ncols+0],pvname) == 0 ) return table[ii*ncols+2];
	}
	return nothing;
}
// adoSetString defined in epics2ado.cxx
// set ADO parameter to paramValue
int adoSetString(const char* adoName, const char* paramName, const char* paramValue, const int type);

// pv_changed - called in the EPICS event loop to react on PV change
static void pv_changed(pv* pv)
{
	// get changed PV
	char *str = val2str(pv->value,pv->dbrType,0);;
	int type = pv->dbrType;
    //TODO/for (i=0; i<pv->nElems; ++i) {
	if(gVerb&VERB_DETAILED) printf("PV %s changed to value='%s' (type=%i, count=%li)\n",pv->name, str, type, pv->nElems);
    //print_time_val_sts(pv, reqElems);
	//update ADO
	adoSetString(gAdoName, pv2param(pv->name,gpv2ado_map,gncols,gnPvs), str, type);
}
//,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

#define VALID_DOUBLE_DIGITS 18  /* Max usable precision for a double */

static unsigned long reqElems = 0;
static unsigned long eventMask = DBE_VALUE | DBE_ALARM;   /* Event mask used */
static int floatAsString = 0;                             /* Flag: fetch floats as string */
static int nConn = 0;                                     /* Number of connected PVs */



/*+**************************************************************************
 *
 * Function:	event_handler
 *
 * Description:	CA event_handler for request type callback
 * 		Prints the event data
 *
 * Arg(s) In:	args  -  event handler args (see CA manual)
 *
 **************************************************************************-*/

static void event_handler (evargs args)
{
    pv* pv = args.usr;

    pv->status = args.status;
    if (args.status == ECA_NORMAL)
    {
        pv->dbrType = args.type;
        pv->nElems = args.count;
        pv->value = (void *) args.dbr;    /* casting away const */

        pv_changed(pv);
        fflush(stdout);

        pv->value = NULL;
    }
}

/*+**************************************************************************
 *
 * Function:	connection_handler
 *
 * Description:	CA connection_handler
 *
 * Arg(s) In:	args  -  connection_handler_args (see CA manual)
 *
 **************************************************************************-*/

static void connection_handler ( struct connection_handler_args args )
{
    pv *ppv = ( pv * ) ca_puser ( args.chid );
    if ( args.op == CA_OP_CONN_UP ) {
        nConn++;
        if (!ppv->onceConnected) {
            ppv->onceConnected = 1;
                                /* Set up pv structure */
                                /* ------------------- */

                                /* Get natural type and array count */
            ppv->dbfType = ca_field_type(ppv->ch_id);
            ppv->dbrType = dbf_type_to_DBR_TIME(ppv->dbfType); /* Use native type */
            if (dbr_type_is_ENUM(ppv->dbrType))                /* Enums honour -n option */
            {
                if (enumAsNr) ppv->dbrType = DBR_TIME_INT;
                else          ppv->dbrType = DBR_TIME_STRING;
            }
            else if (floatAsString &&
                     (dbr_type_is_FLOAT(ppv->dbrType) || dbr_type_is_DOUBLE(ppv->dbrType)))
            {
                ppv->dbrType = DBR_TIME_STRING;
            }
                                /* Set request count */
            ppv->nElems   = ca_element_count(ppv->ch_id);
            ppv->reqElems = reqElems > ppv->nElems ? ppv->nElems : reqElems;

                                /* Issue CA request */
                                /* ---------------- */
            /* install monitor once with first connect */
            ppv->status = ca_create_subscription(ppv->dbrType,
                                                ppv->reqElems,
                                                ppv->ch_id,
                                                eventMask,
                                                event_handler,
                                                (void*)ppv,
                                                NULL);
        }
    }
    else if ( args.op == CA_OP_CONN_DOWN ) {
        nConn--;
        ppv->status = ECA_DISCONN;
        print_time_val_sts(ppv, reqElems);
    }
}


/*+**************************************************************************
 *
 * Function:	main
 *
 * Description:	camonitor main()
 * 		Evaluate command line options, set up CA, connect the
 * 		channels, collect and print the data as requested
 *
 * Arg(s) In:	[options] <pv-name> ...
 *
 * Arg(s) Out:	none
 *
 * Return(s):	Standard return code (0=success, 1=error)
 *
 **************************************************************************-*/

int main (int argc, char *argv[])
{
    int returncode = 0;
    int n;
    int result;                 /* CA result */
    IntFormatT outType;         /* Output type */

    int opt;                    /* getopt() current option */
    int digits = 0;             /* getopt() no. of float digits */

    //int nPvs;                   /* Number of PVs */
    pv* pvs;                    /* Array of PV structures */

    LINE_BUFFER(stdout);        /* Configure stdout buffering */

    while ((opt = getopt(argc, argv, ":nhm:sSe:f:g:l:#:0:w:t:p:F:v:")) != -1) {
        switch (opt) {
        case 'h':               /* Print usage */
            usage(argv[0]);
            return 0;
        case 'n':               /* Print ENUM as index numbers */
            enumAsNr=1;
            break;
        case 't':               /* Select timestamp source(s) and type */
            tsSrcServer = 0;
            tsSrcClient = 0;
            {
                int i = 0;
                char c;
                while ((c = optarg[i++]))
                    switch (c) {
                    case 's': tsSrcServer = 1; break;
                    case 'c': tsSrcClient = 1; break;
                    case 'n': break;
                    case 'r': tsType = relative; break;
                    case 'i': tsType = incremental; break;
                    case 'I': tsType = incrementalByChan; break;
                    default :
                        fprintf(stderr, "Invalid argument '%c' "
                                "for option '-t' - ignored.\n", c);
                    }
            }
            break;
        case 'w':               /* Set CA timeout value */
            if(epicsScanDouble(optarg, &caTimeout) != 1)
            {
                fprintf(stderr, "'%s' is not a valid timeout value "
                        "- ignored. ('camonitor -h' for help.)\n", optarg);
                caTimeout = DEFAULT_TIMEOUT;
            }
            break;
        case '#':               /* Array count */
            if (sscanf(optarg,"%ld", &reqElems) != 1)
            {
                fprintf(stderr, "'%s' is not a valid array element count "
                        "- ignored. ('camonitor -h' for help.)\n", optarg);
                reqElems = 0;
            }
            break;
        case 'p':               /* CA priority */
            if (sscanf(optarg,"%u", &caPriority) != 1)
            {
                fprintf(stderr, "'%s' is not a valid CA priority "
                        "- ignored. ('camonitor -h' for help.)\n", optarg);
                caPriority = DEFAULT_CA_PRIORITY;
            }
            if (caPriority > CA_PRIORITY_MAX) caPriority = CA_PRIORITY_MAX;
            break;
        case 'm':               /* Select CA event mask */
            eventMask = 0;
            {
                int i = 0;
                char c, err = 0;
                while ((c = optarg[i++]) && !err)
                    switch (c) {
                    case 'v': eventMask |= DBE_VALUE; break;
                    case 'a': eventMask |= DBE_ALARM; break;
                    case 'l': eventMask |= DBE_LOG; break;
                    case 'p': eventMask |= DBE_PROPERTY; break;
                        default :
                            fprintf(stderr, "Invalid argument '%s' "
                                    "for option '-m' - ignored.\n", optarg);
                            eventMask = DBE_VALUE | DBE_ALARM;
                            err = 1;
                    }
            }
            break;
        case 's':               /* Select string dbr for floating type data */
            floatAsString = 1;
            break;
        case 'S':               /* Treat char array as (long) string */
            charArrAsStr = 1;
            break;
        case 'e':               /* Select %e/%f/%g format, using <arg> digits */
        case 'f':
        case 'g':
            if (sscanf(optarg, "%d", &digits) != 1)
                fprintf(stderr,
                        "Invalid precision argument '%s' "
                        "for option '-%c' - ignored.\n", optarg, opt);
            else
            {
                if (digits>=0 && digits<=VALID_DOUBLE_DIGITS)
                    sprintf(dblFormatStr, "%%-.%d%c", digits, opt);
                else
                    fprintf(stderr, "Precision %d for option '-%c' "
                            "out of range - ignored.\n", digits, opt);
            }
            break;
        case 'l':               /* Convert to long and use integer format */
        case '0':               /* Select integer format */
            switch ((char) *optarg) {
            case 'x': outType = hex; break;    /* x print Hex */
            case 'b': outType = bin; break;    /* b print Binary */
            case 'o': outType = oct; break;    /* o print Octal */
            default :
                outType = dec;
                fprintf(stderr, "Invalid argument '%s' "
                        "for option '-%c' - ignored.\n", optarg, opt);
            }
            if (outType != dec) {
              if (opt == '0') outTypeI = outType;
              else            outTypeF = outType;
            }
            break;
        case 'F':               /* Store this for output and tool_lib formatting */
            fieldSeparator = (char) *optarg;
            break;
        case 'v': gVerb = atoi(optarg); fprintf(stderr,"verbosity set to %i\n",gVerb); break;
        case '?':
            fprintf(stderr,
                    "Unrecognized option: '-%c'. ('camonitor -h' for help.)\n",
                    optopt);
            return 1;
        case ':':
            fprintf(stderr,
                    "Option '-%c' requires an argument. ('camonitor -h' for help.)\n",
                    optopt);
            return 1;
        default :
            usage(argv[0]);
            return 1;
        }
    }
    if(argc - optind != 2)
    {
        fprintf(stderr, "Two arguments expected: ADO name and csv map file\n");
        return 1;
    }
    gAdoName = argv[optind];
    //'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
    // select records of type 'epics < ado'
    printf("ADO: %s, map file: %s\n",gAdoName,argv[optind+1]);
    gnPvs = parse_epics2ado_csvmap(argv[optind+1],'>',&gncols,gpv2ado_map,MAXTOKENS,gstorage,MAXTOKENS*MAXTOKENSIZE);
    if(gnPvs==0) {fprintf(stderr, "No PV's in the map file with '>' direction.\n"); return 1;}

                                /* Start up Channel Access */

    result = ca_context_create(ca_disable_preemptive_callback);
    if (result != ECA_NORMAL) {
        fprintf(stderr, "CA error %s occurred while trying "
                "to start channel access.\n", ca_message(result));
        return 1;
    }
                                /* Allocate PV structure array */

    pvs = calloc (gnPvs, sizeof(pv));
    if (!pvs)
    {
        fprintf(stderr, "Memory allocation for channel structures failed.\n");
        return 1;
    }
                                /* Connect channels */

                                      /* Copy PV names from command line */
    for (n = 0; n < gnPvs; n++)
    {
        pvs[n].name   = gpv2ado_map[n*gncols + 0];
        if(gVerb&VERB_INFO) printf("Monitor epics PV: %s, update ADO: %s.%s\n",pvs[n].name,gAdoName,pv2param(pvs[n].name,gpv2ado_map,gncols,gnPvs));
    }
                                      /* Create CA connections */
    returncode = create_pvs(pvs, gnPvs, connection_handler);
    if ( returncode ) {
        return returncode;
    }
                                      /* Check for channels that didn't connect */
    ca_pend_event(caTimeout);
    for (n = 0; n < gnPvs; n++)
    {
        if (!pvs[n].onceConnected)
            print_time_val_sts(&pvs[n], reqElems);
    }

                                /* Read and print data forever */
    printf("Event loop started...\n");
    ca_pend_event(0);

                                /* Shut down Channel Access */
    ca_context_destroy();

    return result;
}
