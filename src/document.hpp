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


// note that these are all subect to fuzzy tolerance
enum class intersect_result {
	distinct,

	// at least one vertex, edge, or face touches
	touching,

	// there's some volume that overlaps
	overlap,
};

intersect_result classify_solid_intersection(
	const TopoDS_Shape& shape, const TopoDS_Shape& tool, double fuzzy_value,
	double &vol_common, double &vol_cut, double &vol_cut12);
