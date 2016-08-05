# epics2ado
ADO to EPICS bridge lane: monitor EPICS PVs and cnange corresponding ADO variables

ADO to EPICS one-lane bridge

## Dependencies

- EPICS
- ADO
- ECLIPS
- ClearCase

## Usage

epicsado program  monitors EPICS PVs defined in the supplied map file and modifies corresponding ADO variables.

Example for simple.test ADO, assuming the softIOC is already running with proper db (see below):

-epics2ado simple.test epics2ado_simple.csv -v1

To set ADO variable char to 10 from EPICS:

:caput charS 10

The script ado2epics_map.sh generates map file from live ADO. 

Example for simple.test ADO:

:./ado2epics_map.sh >! epics2ado_simple.csv

The script ado2epics_db.sh generates EPICS database from live ADO. 
The generated map is csv file and it defines all ADO variables to be exported to the EPICS PVs with the same name.
Example for simple.test ADO:

:./ado2epics_db.sh simple.test >! simple.db

Example of running softIOC with the simple.db:

:softIoc -d simple.db

## Compilation

see https://github.com/ASukhanov/ado2epics
