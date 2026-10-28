#include <assert.h>
#include <string.h>

#include "../source/launcher_run_report.h"

int main(void)
{
    char name[32];

    launcher_run_report_name( "/games/My Game/demo.EXE", name, sizeof(name) );
    assert( !strcmp( name, "demo" ) );
    launcher_run_report_name( "C:\\Program Files\\Title!x.exe", name, sizeof(name) );
    assert( !strcmp( name, "Title-x" ) );
    launcher_run_report_name( "/games/.exe", name, sizeof(name) );
    assert( !strcmp( name, ".exe" ) );
    launcher_run_report_name( "/games/ThisIsALongExecutableName.exe", name, 9 );
    assert( strlen( name ) == 8 && !strcmp( name, "ThisIsAL" ) );
    launcher_run_report_name( "/games/no-room.exe", name, 0 );
    assert( !strcmp( name, "ThisIsAL" ) );
    return 0;
}
