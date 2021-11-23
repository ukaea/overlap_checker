// this tool produces a "known good" gluing of a brep file that can be used
// for regression testing our changes to the code. having this before we start
// work on a multithreaded implementation seems especially useful

// this is here because it uses the Geom module from the SALOME platform,
// which we don't want to include due to its size

// which I compiled via:
//
//   clang++ -o salome_geom_gluer2 salome_geom_gluer2.cpp \
//      -I/usr/include/opencascade -lTKernel -lTKBRep  -lTKG3d -lTKMath -lTKXCAF \
//      -I"$SGeom/src/GEOMAlgo" "$SGeom/build/src/GEOMAlgo/libGEOMAlgo.so" \
//
// where $SGeom is a built copy of https://github.com/ukaea/SGeom

#include <iostream>

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>

#include <GEOMAlgo_Gluer2.hxx>

int
main(int, char **argv)
{
	TopoDS_Shape shape;

	if (!BRepTools::Read(shape, argv[1], BRep_Builder{})) {
		std::cerr << "failed to read brep file: " << argv[1] << '\n';
		return 1;
	}

	std::cerr << "read brep file: " << argv[1] << '\n';

	GEOMAlgo_Gluer2 gluer;
	gluer.SetArgument(shape);
	gluer.SetKeepNonSolids(false);

	gluer.Detect();
	if (gluer.ErrorStatus()) {
		std::cerr << "glue detect failed!\n";
		return 1;
	}

	gluer.Perform();
	if (gluer.ErrorStatus()) {
		std::cerr << "shape glue failed!\n";
		return 1;
	}

	if (gluer.WarningStatus()) {
		std::cerr << "got warnings from shape glue!\n";
	}

	shape = gluer.Shape();

	std::cerr << "shape glued!\n";

	if (!BRepTools::Write(shape, argv[2])) {
		std::cerr << "failed to write brep file: " << argv[2] << '\n';
		return 1;;
	}

	std::cerr << "wrote brep file: " << argv[2] << '\n';

	return 0;
}
