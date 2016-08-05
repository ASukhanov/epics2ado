//'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
/* ADO side of the epics to ado bridge
 *
 * version v02 2016-07-11 by &RA. Using SetAsync/GetAsync
 * version v03 2016-07-15 by &RA.
 * version v04 2016-07-15 by &RA. value type transferred to adoSetString
 * version v05 2016-07-15 by &RA. Better printing.
 * version v06 2016-08-01 by &RA. TIMESTAMPING.
 */
/*TODO.
 * The function is not tuned for performance, although it is not CPU hungry.
 * The new AdoIf is constructed on each call to adoSetString. Surprisingly, it
 * does not cause the memory growth on repetitive calls.
 * It is probably make sense to avoid the re-creating of the AdoIf.
 */
#include "adoIf/adoIf.hxx"
#include "rhicError/rhicError.h"

#undef GET_BEFORE_SET

//#define ASYNC // Use SetAsync to set parameters in ADO.

#ifndef ASYNC
//'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
// adoSetString: set ado parameter

// reverse engineered DBR types:
#define DBR_DOUBLE 20
#define DBR_STRING 14

#define VERB_INFO 1
#define VERB_DEBUG 2
#define VERB_DETAILED 4
extern "C" int gVerb;

extern "C" int adoSetString(const char* adoName, const char* paramName, const char* paramValue, const int type)
{
#define TIMESTAMPING
#ifdef TIMESTAMPING
#define TMPSTRLEN 100
	char tmpstr[TMPSTRLEN];
	//int lpname = strlen(paramName);
	struct timespec ts_now;
	clock_gettime(CLOCK_REALTIME,&ts_now);
	snprintf(tmpstr,TMPSTRLEN,"%s:%s",paramName,"timestampSeconds");
#endif
	// create AdoIf for the instance someAdo
	if(gVerb&VERB_DEBUG) printf("ADO %s.%s\tset to %s, type %i\n",adoName,paramName,paramValue,type);
	AdoIf a(adoName);
	if(a.CreateOK()!=0) { // check the creation status
		printf("AdoIf failed : %s\n",
				RhicErrorNumToErrorStr(a.CreateOK()) );
		exit(1);
	}
	int stat;
#ifdef GET_BEFORE_SET
	// create a value object and use it in Get to get the
	// parameter "p"
	Value v(1.0);
	stat = a.Get(paramName, &v);
	if(stat!=0) { // check the status
		if(stat==ADO_FAILED) {
			const int * indStat = a.GetStatuses();
			printf("Get for %s failed: %d=%s\n", a.AdoName(),
					indStat[0], RhicErrorNumToErrorStr(indStat[0]));
		}
		else { // some other error
			printf("ERR: %s\n", RhicErrorNumToErrorStr(stat));
		}
		exit(2);
	}
	// all went well
	char *aString = v.StringVal(' ');
	printf("Got the value '%s' from '%s' successfully\n", aString,
			a.AdoName());
	delete aString;
#endif //GET_BEFORE_SET

	// If ADO parameter is defined as number, then it will not accept the string value.
	// The different Set() functions are used depending of data type.
	if(type == DBR_DOUBLE)
	{
		if(gVerb&VERB_DETAILED) printf("DBR_DOUBLE=%g\n",atof(paramValue));
		stat = a.Set(paramName, Value(atof(paramValue)));
		//stat = a.Set(paramName, Value(paramValue));
	}
	else
	{
		if(gVerb&VERB_DETAILED) printf("DBR_STRING=%s\n",(char*)Value(paramValue));
		stat = a.Set(paramName, Value(paramValue));
	}

	if(stat!=0) { // check the status
		if(stat==ADO_FAILED) {
			const int * indStat = a.GetStatuses();
			printf("Set for %s failed: %d=%s\n", a.AdoName(),
					indStat[0], RhicErrorNumToErrorStr(indStat[0]));
		}
		else { // some other error
			printf("ERR: %s for %s type %i\n", RhicErrorNumToErrorStr(stat), paramName, type);
		}
		//&RA/exit(3);
		return 1;
	}
#ifdef TIMESTAMPING
	if(gVerb&VERB_DETAILED) printf("Timestamping: %s %i\n",tmpstr,(int)(ts_now.tv_sec));
	a.Set(tmpstr, Value((int)(ts_now.tv_sec)));
#endif
	return 0;
}
//,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
#endif //ndef ASYNC
#ifdef ASYNC
//'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
// error callback
int errcb (AdoIf *a, const char* propertyID, const int adoStatus[], int const paramStatus[],
           const AsyncSetup *setup, void *arg, const void *reqId)
{
  printf("-------for %s status is-------\n", propertyID);
  printf("ado error is  %s\n", RhicErrorNumToErrorStr(adoStatus[0]));
  if(adoStatus[0]==ADO_FAILED)printf("param error is    %s\n", RhicErrorNumToErrorStr(paramStatus[0]));
  return TRUE;
}
extern "C" int adoSetString(char* adoName, char* paramName, char* paramValue)
{
//int main(int argc, char *argv[])
{
  // create the async handler
  AsyncHandler *ah = new AsyncHandler();

  // create AsyncSetup object for SetAsync
  AsyncSetup *ssetup = new SetAsyncSetup (errcb);

  // always receive status
  ssetup->SetReceiveStatus();

  // create adoif
  AdoIf *a = new AdoIf(adoName);
  if(a->CreateOK()!=0){
    printf("%s\n", RhicErrorNumToErrorStr(errno));
    exit(0);
  }

  // create Value object to send data
  Value **data = (Value **)malloc(sizeof(Value *)*2);
  data[1] = NULL;

  // call SetAsync
  Value *val = new Value(33);
  data[0] = val;
  cout<<">SetAsync "<<val<<endl;
  int stat = a->SetAsync(paramName, ssetup, paramValue);
  delete val;

  // handle events
  ah->HandleEvents();
  return 0;
}
//,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
#endif //ASYNC
