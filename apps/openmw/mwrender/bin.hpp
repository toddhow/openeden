#ifndef VSGOPENMW_MWRENDER_BIN_H
#define VSGOPENMW_MWRENDER_BIN_H

namespace MWRender
{
    // vsgopenmw-bin-policy=PREFER_ORDERED_NODES
    enum Bin
    {
        Bin_DepthSorted = 0,
        Bin_Compute = /*ExternalBin::*/ 1,
        // Bin_FirstPerson = 2,
        Bin_SunGlare = 3,
        // OpenEden (task 0041): the volumetric fog's compute passes, in a graph of their own.
        Bin_Volume = 4,
        // OpenEden (task 0042): the volumetric clouds' compute passes, in a graph of their own.
        Bin_Cloud = 5
    };
}

#endif
