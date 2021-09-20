#include <vector>

// from opencascade
#include <TopoDS_Shape.hxx>

double volume_of_shape(const class TopoDS_Shape& shape);
double distance_between_shapes(const TopoDS_Shape& a, const TopoDS_Shape& b);

struct document {
	std::vector<TopoDS_Shape> solid_shapes;

	// these just exit on error, will do something better when it's clear what
	// that is!
	void load_brep_file(const char* path);

	size_t count_invalid_shapes();
};


// note that these are all subect to fuzzy tolerance
enum class intersect_status {
	// something failed within OCCT, a different fuzzy value might help
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


enum class imprint_status {
	// something failed within OCCT, using a different fuzzy value might help
	failed,

	// no volume in common
	distinct,

	// common area was merged into whichever side
	merge_into_shape,
	merge_into_tool,
};

struct imprint_result {
	imprint_status status;

	double fuzzy_value;

	// might want more than this later!
	int num_filler_warnings, num_common_warnings, num_fuse_warnings;

	// following only valid if status != failed

	// intermediate volumes
	double vol_common, vol_cut, vol_cut12;

	// results
	TopoDS_Shape shape, tool;
};

imprint_result perform_solid_imprinting(
	const TopoDS_Shape& shape, const TopoDS_Shape& tool, double fuzzy_value);
