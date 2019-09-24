#pragma once
#include "MeshSemantics.h"

class Arguments;

class Exporter : public MPxCommand {
  public:
    Exporter();
    ~Exporter();

    static void *createInstance();

    MStatus doIt(const MArgList &args) override;

    bool isUndoable() const override;

    bool hasSyntax() const override;

    static void exportScene(const Arguments &args);

  private:
    DISALLOW_COPY_MOVE_ASSIGN(Exporter);
    MStatus run(const MArgList &args) const;
    static void printFatalError();
};
