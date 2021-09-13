#include <vector>

// from opencascade
#include <TopoDS_Shape.hxx>


bool are_vals_close(double a, double b, double drel=1e-10, double dabs=1e-13);

double volume_of_shape(const class TopoDS_Shape& shape);
double distance_between_shapes(const TopoDS_Shape& a, const TopoDS_Shape& b);

struct document {
	std::vector<TopoDS_Shape> solid_shapes;

	// these just abort on errors, will do something better when it's clear
	// what that is!
	void load_brep_file(const char* path);
};


// note that these are all subect to fuzzy tolerance
enum class intersect_status {
	// something failed within OCC, different fuzzy values might help
	failed,

	// null intersection
	distinct,

	// at least one vertex, edge, or face touches
	touching,

	// there's some volume that overlaps
	overlap,
};

struct intersect_result {
	intersect_status status;

	// the minimum fuzzy value is 1e-9, this reports the correct value if
	// something smaller is requested
	double fuzzy_value;

	// might want more than this later!
	int num_filler_warnings, num_common_warnings, num_section_warnings;

	// only valid if result == overlap
	double vol_common, vol_cut, vol_cut12;
};

intersect_result classify_solid_intersection(
	const TopoDS_Shape& shape, const TopoDS_Shape& tool, double fuzzy_value);
