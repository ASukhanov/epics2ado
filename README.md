# epics2ado: EPICS to ADO bridge lane: monitor EPICS PVs and cnange corresponding ADO variables

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

## Compilation

see https://github.com/ASukhanov/ado2epics
