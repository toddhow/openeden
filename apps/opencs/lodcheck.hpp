#ifndef CS_LODCHECK_H
#define CS_LODCHECK_H

namespace CS
{
    /// Whether the command line asks for the tile viewer check (--open-lod-pack).
    bool isLodPackRequest(int argc, char** argv);

    /// Opens a baked LOD tile pack in the tile viewer with no game data, no GPU and no editor, and writes its
    /// pictures as PNG files into the screenshots folder of the user data folder, so they can be looked at without a
    /// window grab: lod-map.png (the world map), lod-height.png, lod-normal.png, lod-albedo.png (when the tile has
    /// one) and lod-panel.png (the whole panel as Qt draws it).
    ///
    /// eden-studio --open-lod-pack PATH [--lod-camera x,y,z,heading,fov,viewport,threshold,mode] [--lod-tile ROW]
    /// [--auto-screenshot N --auto-quit M]
    ///
    /// The camera fields are those of CSMLod::parseCameraSpec (heading and fov in degrees, mode auto, tile or live).
    /// The frame counts are accepted and ignored: nothing here is drawn by a GPU. One log line sums up the result.
    /// \return the process's exit code
    int runLodPackCheck(int argc, char** argv);
}

#endif
