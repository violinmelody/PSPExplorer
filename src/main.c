#include <pspkernel.h>
#include "app.h"

PSP_MODULE_INFO("PSPExplorer",0,1,0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER|THREAD_ATTR_VFPU);

int main(void){
	return app_run();
}
