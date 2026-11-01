#ifndef CS_CAPTURE_H
#define CS_CAPTURE_H

namespace CS
{
    /// Whether the command line asks for an unattended capture (--open-cell).
    bool isCaptureRequest(int argc, char** argv);

    /// Opens the content files of the configuration, shows one cell and writes a frame of it to the screenshots folder,
    /// with no person present: no editor window, no dialogue and no IPC server, and its one window takes no focus.
    ///
    /// eden-studio --open-cell X --auto-screenshot N --auto-quit M [--camera ex,ey,ez,cx,cy,cz,ux,uy,uz] [--capture-views
    /// V]
    ///
    /// X is a cell's record ID ("#-2 -9") or its name. N and M count frames. --camera sets the view as eye, centre and
    /// up.
    /// --capture-views shows the cell in V views on one device, each writing its own file. The usual --config,
    /// --replace and --user-data options apply. \return the process's exit code
    int runCapture(int argc, char** argv);
}

#endif
