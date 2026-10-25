#include "center-snap.h"
#include <cstdlib>

int main()
{
    bool latched=false;
    if(StreamHubCenterSnap(11,10,latched)) return EXIT_FAILURE;
    if(!StreamHubCenterSnap(9,10,latched)) return EXIT_FAILURE;
    if(!StreamHubCenterSnap(-19,10,latched)) return EXIT_FAILURE;
    if(StreamHubCenterSnap(21,10,latched)) return EXIT_FAILURE;
    if(StreamHubCenterSnap(15,10,latched)) return EXIT_FAILURE;
    if(!StreamHubCenterSnap(0,10,latched)) return EXIT_FAILURE;
    if(StreamHubCenterSnap(0,0,latched)) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
