/*
    This file exists ONLY to satisfy CMake's requirement for a non-empty source list
    when defining a library target.

    At early stages of the project, we define the `usblink` library before having
    actual implementation files (.cpp). Some toolchains and IDE generators (e.g., Xcode,
    Visual Studio) may fail or behave inconsistently when a library target has no sources.

    This dummy translation unit ensures:
    - The library target is considered valid
    - The build system remains stable
    - The project structure can be established early

    IMPORTANT:
    This file must be removed as soon as real implementation files are added
    (e.g., WindowsSerialTransport.cpp, PacketEncoder.cpp, etc.)
*/

namespace usblink
{
    void dummy()
    {
        // Intentionally empty
    }
}