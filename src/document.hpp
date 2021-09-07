#include <vector>

// from opencascade
#include <TopoDS_Shape.hxx>


bool are_vals_close(double a, double b, double drel=1e-10, double dabs=1e-13);

double volume_of_shape(const class TopoDS_Shape& shape);

struct document {
	std::vector<TopoDS_Shape> solid_shapes;

	// these just abort on errors, will do something better when it's clear
	// what that is!
	void load_brep_file(const char* path);
};


bool is_trivial_union_fuse(class BRepAlgoAPI_Fuse& op);
bool is_enclosure_fuse(class BRepAlgoAPI_Fuse& op);
