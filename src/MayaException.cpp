#include "MayaException.h"
#include "externals.h"

void MayaException::throwIt(const MStatus status, const std::string &message,
                            const char *file, const int line,
                            const char *function) {
    std::stringstream err;
    std::string path(file);
    std::replace(path.begin(), path.end(), '\\', '/');
    err << prefix << "*** ERROR *** " << message << " ["
        << status.errorString().asChar() << "] in " << function << " (" << path
        << ":" << line << ")";
    throw MayaException(status, err.str());
}

MStatus MayaException::printError(const std::string &message,
                                  const MStatus &status) {
    MGlobal::executeCommand(MString("error ") + escaped(message).c_str());
    cerr << prefix << "*** ERROR *** " << message << " ["
         << status.errorString().asChar() << "]" << endl;
    return status;
}

void MayaException::printError(const std::string &message) {
    MGlobal::executeCommand(MString("error ") + escaped(message).c_str());
    cerr << prefix << "*** ERROR *** " << message << endl;
}
