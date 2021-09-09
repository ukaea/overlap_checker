#include <string>
#include <vector>
#include <deque>
#include <condition_variable>

#include <pthread.h>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>
#include <spdlog/stopwatch.h>
#include <spdlog/pattern_formatter.h>

#include <TopoDS_Iterator.hxx>

#include <BRepCheck_Analyzer.hxx>

#include <BRepBndLib.hxx>
#include <Bnd_OBB.hxx>

#include "document.hpp"


// trim from left
static std::string& ltrim_inplace(std::string& s, const char* t = " \t\n\r\f\v")
{
    s.erase(0, s.find_first_not_of(t));
    return s;
}

// trim from right
static std::string& rtrim_inplace(std::string& s, const char* t = " \t\n\r\f\v")
{
    s.erase(s.find_last_not_of(t) + 1);
    return s;
}

// trim from left & right
static std::string& trim_inplace(std::string& s, const char* t = " \t\n\r\f\v")
{
    return ltrim_inplace(rtrim_inplace(s, t), t);
}

// copying versions
inline std::string ltrim(std::string s, const char* t = " \t\n\r\f\v")
{
    return ltrim_inplace(s, t);
}

inline std::string rtrim(std::string s, const char* t = " \t\n\r\f\v")
{
    return rtrim_inplace(s, t);
}

inline std::string trim(std::string s, const char* t = " \t\n\r\f\v")
{
    return trim_inplace(s, t);
}

class my_formatter_flag : public spdlog::custom_flag_formatter
{
    using clock = std::chrono::steady_clock;
	using timepoint = std::chrono::time_point<clock>;

    timepoint reference;

public:
	my_formatter_flag() : reference{clock::now()} {}
	my_formatter_flag(timepoint ref) : reference{ref} {}

	void format(const spdlog::details::log_msg &, const std::tm &, spdlog::memory_buf_t &dest) override
    {
		auto elapsed = std::chrono::duration<double>(clock::now() - reference);
		auto txt = fmt::format("{:.3f}", elapsed.count());
        dest.append(txt.data(), txt.data() + txt.size());
    }

    std::unique_ptr<custom_flag_formatter> clone() const override
    {
        return spdlog::details::make_unique<my_formatter_flag>(reference);
    }
};

template <> struct fmt::formatter<Bnd_OBB>: formatter<string_view> {
  // parse is inherited from formatter<string_view>.
	template <typename FormatContext>
	auto format(Bnd_OBB obb, FormatContext& ctx) {
		std::stringstream ss;
		ss << '{';
		obb.DumpJson(ss);
		ss << '}';
		return formatter<string_view>::format(ss.str(), ctx);
	}
};

template <> struct fmt::formatter<TopAbs_ShapeEnum>: formatter<string_view> {
	// parse is inherited from formatter<string_view>.
	template <typename FormatContext>
	auto format(TopAbs_ShapeEnum c, FormatContext& ctx) {
		string_view name = "unknown";
		switch (c) {
		case TopAbs_COMPOUND: name = "COMPOUND"; break;
		case TopAbs_COMPSOLID: name = "COMPSOLID"; break;
		case TopAbs_SOLID: name = "SOLID"; break;
		case TopAbs_SHELL: name = "SHELL"; break;
		case TopAbs_FACE: name = "FACE"; break;
		case TopAbs_WIRE: name = "WIRE"; break;
		case TopAbs_EDGE: name = "EDGE"; break;
		case TopAbs_VERTEX: name = "VERTEX"; break;
		case TopAbs_SHAPE: name = "SHAPE"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};

template <> struct fmt::formatter<BRepCheck_Status>: formatter<string_view> {
	// parse is inherited from formatter<string_view>.
	template <typename FormatContext>
	auto format(BRepCheck_Status c, FormatContext& ctx) {
		string_view name = "unknown";
		switch(c) {
		case BRepCheck_NoError: name = "NoError"; break;
		case BRepCheck_InvalidPointOnCurve: name = "InvalidPointOnCurve"; break;
		case BRepCheck_InvalidPointOnCurveOnSurface: name = "InvalidPointOnCurveOnSurface"; break;
		case BRepCheck_InvalidPointOnSurface: name = "InvalidPointOnSurface"; break;
		case BRepCheck_No3DCurve: name = "No3DCurve"; break;
		case BRepCheck_Multiple3DCurve: name = "Multiple3DCurve"; break;
		case BRepCheck_Invalid3DCurve: name = "Invalid3DCurve"; break;
		case BRepCheck_NoCurveOnSurface: name = "NoCurveOnSurface"; break;
		case BRepCheck_InvalidCurveOnSurface: name = "InvalidCurveOnSurface"; break;
		case BRepCheck_InvalidCurveOnClosedSurface: name = "InvalidCurveOnClosedSurface"; break;
		case BRepCheck_InvalidSameRangeFlag: name = "InvalidSameRangeFlag"; break;
		case BRepCheck_InvalidSameParameterFlag: name = "InvalidSameParameterFlag"; break;
		case BRepCheck_InvalidDegeneratedFlag: name = "InvalidDegeneratedFlag"; break;
		case BRepCheck_FreeEdge: name = "FreeEdge"; break;
		case BRepCheck_InvalidMultiConnexity: name = "InvalidMultiConnexity"; break;
		case BRepCheck_InvalidRange: name = "InvalidRange"; break;
		case BRepCheck_EmptyWire: name = "EmptyWire"; break;
		case BRepCheck_RedundantEdge: name = "RedundantEdge"; break;
		case BRepCheck_SelfIntersectingWire: name = "SelfIntersectingWire"; break;
		case BRepCheck_NoSurface: name = "NoSurface"; break;
		case BRepCheck_InvalidWire: name = "InvalidWire"; break;
		case BRepCheck_RedundantWire: name = "RedundantWire"; break;
		case BRepCheck_IntersectingWires: name = "IntersectingWires"; break;
		case BRepCheck_InvalidImbricationOfWires: name = "InvalidImbricationOfWires"; break;
		case BRepCheck_EmptyShell: name = "EmptyShell"; break;
		case BRepCheck_RedundantFace: name = "RedundantFace"; break;
		case BRepCheck_InvalidImbricationOfShells: name = "InvalidImbricationOfShells"; break;
		case BRepCheck_UnorientableShape: name = "UnorientableShape"; break;
		case BRepCheck_NotClosed: name = "NotClosed"; break;
		case BRepCheck_NotConnected: name = "NotConnected"; break;
		case BRepCheck_SubshapeNotInShape: name = "SubshapeNotInShape"; break;
		case BRepCheck_BadOrientation: name = "BadOrientation"; break;
		case BRepCheck_BadOrientationOfSubshape: name = "BadOrientationOfSubshape"; break;
		case BRepCheck_InvalidPolygonOnTriangulation: name = "InvalidPolygonOnTriangulation"; break;
		case BRepCheck_InvalidToleranceValue: name = "InvalidToleranceValue"; break;
		case BRepCheck_EnclosedRegion: name = "EnclosedRegion"; break;
		case BRepCheck_CheckFail: name = "CheckFail"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};


static bool
is_shape_valid(const TopoDS_Shape& shape)
{
	BRepCheck_Analyzer checker(shape);

	if (checker.IsValid()) {
		return true;
	}
	std::vector<BRepCheck_Status> errors;

	const auto &result = checker.Result(shape);

	for (const auto &status : result->StatusOnShape()) {
		if (status != BRepCheck_NoError) {
			errors.push_back(status);
		}
	}

	for (TopoDS_Iterator it{shape}; it.More(); it.Next()) {
		const auto &component = it.Value();

		if (checker.IsValid(component)) {
			continue;
		}

		for (const auto &status : result->StatusOnShape(component)) {
			if (status != BRepCheck_NoError) {
				errors.push_back(status);
			}
		}
	}

	spdlog::warn(
		"shape contains following errors {}",
		errors);

	return false;
}

static bool
are_bboxs_disjoint(const Bnd_OBB &b1, const Bnd_OBB& b2, double tolerance)
{
	if (tolerance > 0) {
		Bnd_OBB e1{b1}, e2{b2};
		e1.Enlarge(tolerance);
		e2.Enlarge(tolerance);
		return e1.IsOut(e2);
	}
	return b1.IsOut(b2);
}

struct worker_input {
	size_t hi, lo;
	double fuzzy_value;
};

struct worker_output {
	size_t hi, lo;
	intersect_result result;
	double vol_common;
	double vol_cut;
	double vol_cut12;
};

class worker_queue {
	std::mutex mutex;
	std::condition_variable cond;

	std::deque<worker_input> input;
	std::deque<worker_output> output;

public:
	document &doc;

	worker_queue(document &doc) : doc{doc} {}

	size_t input_size() {
		return input.size();
	}

	void add_work(const worker_input &work) {
		input.push_back(work);
	}

	bool next_input(worker_input &work) {
		std::unique_lock<std::mutex> mlock(mutex);

		if (input.empty()) {
			return false;
		}

		work = input.front();
		input.pop_front();

		return true;
	}

	void add_output(const worker_output &work) {
		std::unique_lock<std::mutex> mlock(mutex);

		output.push_back(work);
		mlock.unlock();

		cond.notify_one();
	}

	worker_output next_output() {
		std::unique_lock<std::mutex> mlock(mutex);
		while (output.empty()) {
			cond.wait(mlock);
        }
		auto result = output.front();
		output.pop_front();
		return result;
	}
};

static void *
worker(void *param)
{
	spdlog::debug("worker thread starting");

	worker_queue *queue = (worker_queue*)param;

	const auto &shapes = queue->doc.solid_shapes;

	worker_input input;
	worker_output output;

	while (queue->next_input(input)) {
		output.result = classify_solid_intersection(
			shapes[output.hi = input.hi],
			shapes[output.lo = input.lo],
			input.fuzzy_value,
			output.vol_common, output.vol_cut, output.vol_cut12);

		queue->add_output(output);
	}

	spdlog::debug("worker thread exiting");
	return nullptr;
}


int
main(int argc, char **argv)
{
	// pull config from environment variables, e.g. `export SPDLOG_LEVEL=info,mylogger=trace`
	spdlog::cfg::load_env_levels();

	auto formatter = std::make_unique<spdlog::pattern_formatter>();
    formatter->add_flag<my_formatter_flag>('*').set_pattern("[%*] [%^%l%$] %v");
    spdlog::set_formatter(std::move(formatter));

	if(argc != 1) {
		fmt::print(stderr, "{} takes no arguments.\n", argv[0]);
		return 1;
	}

	const char *path = "input.brep";

	document doc;
	doc.load_brep_file(path);

	// AFAICT: from this point only doc.solid_shapes is used by original code,
	// searching for mySolids in the original code seems to have lots of
	// relevant hits

	/*
	** Geom::GeometryShapeChecker processor
	*/
	spdlog::info("checking geometry");
	bool all_ok = true;
	for (const auto &shape : doc.solid_shapes) {
		if (!is_shape_valid(shape)) {
			all_ok = false;
		}
	}
	if (!all_ok) {
		spdlog::critical("some shapes were not valid");
		return 1;
	}

	/*
	** Geom::GeometryPropertyBuilder processor
	*/
	if (false) {
		spdlog::info("caching geometry properties");
		// original code runs OccUtils::geometryProperty(shape) on every @shape.

		// why?  just so that _metadata.json could be written out?
		//
		//  * hashes for later referencing material properties seems useful

		// is this just because the code was using the shape Explorer rather
		// than Iterator, hence ordering could get weird
	}

	/*
	** Geom::BoundBoxBuilder processor
	*
	* just use Orientated Bounding Boxes for now, worry about compatibility
	* later!
	*/
	spdlog::info("calculating shape information");
	std::vector<Bnd_OBB> bounding_boxes;
	std::vector<double> volumes;
	bounding_boxes.reserve(doc.solid_shapes.size());
	volumes.reserve(doc.solid_shapes.size());
	for (const auto &shape : doc.solid_shapes) {
		Bnd_OBB obb;
		BRepBndLib::AddOBB(shape, obb);
		bounding_boxes.push_back(obb);

		volumes.push_back(volume_of_shape(shape));
	}

	/*
	** Geom::GeometryImprinter
	*/

	spdlog::info("starting imprinting");

	// need more descriptive names for tolerance and clearance
	const double
		minimum_shape_volume = 1,
		imprint_tolerance = 0.1, // 0.001 is a good default, but larger value causes testable changes
		imprint_clearance = 0.5,
		imprint_max_common_volume_ratio = 0.01;

	if (imprint_tolerance <= 0) {
		spdlog::critical(
			"imprinting tolerance ({}) should be positive",
			imprint_tolerance);
		return 1;
	}

	if (imprint_clearance <= 0) {
		spdlog::critical(
			"imprinting clearance ({}) should be positive",
			imprint_clearance);
		return 1;
	}

	if (imprint_clearance <= imprint_tolerance) {
		spdlog::critical(
			"imprinting clearance ({}) should be larger than tolerance ({})",
			imprint_clearance, imprint_tolerance);
		return 1;
	}

	if (imprint_max_common_volume_ratio <= 0 ||
		imprint_max_common_volume_ratio >= 1) {
		spdlog::critical(
			"imprinting max common volume ratio ({}) should be in (0., 1.)",
			imprint_max_common_volume_ratio);
		return 1;
	}

	worker_queue queue(doc);

	for (size_t hi = 0; hi < doc.solid_shapes.size(); hi++) {
		if (volumes[hi] < minimum_shape_volume) {
			spdlog::info(
				"ignoring shape {} because it's too small, {} < {}",
				hi, volumes[hi], minimum_shape_volume);
			continue;
		}

		for (size_t lo = 0; lo < hi; lo++) {
			if (volumes[lo] < minimum_shape_volume)
				continue;

			// seems reasonable to assume majority of shapes aren't close to
			// overlapping, so check with coarser limit first
			if (are_bboxs_disjoint(
					bounding_boxes[hi], bounding_boxes[lo], imprint_clearance)) {
				continue;
			}

			worker_input work{hi, lo, imprint_tolerance};
			queue.add_work(work);
		}
	}

	{
		size_t remain = queue.input_size();

		spdlog::debug("launching worker threads");
		std::vector<pthread_t> threads;
		for (int i = 0; i < 4; i++) {
			pthread_t tid;
			assert (pthread_create(&tid, NULL, worker, &queue) == 0);
			threads.push_back(tid);
		}
		spdlog::debug("waiting for results from workers");

		while (remain--) {
			worker_output output = queue.next_output();

			size_t hi = output.hi, lo = output.lo;

			switch (output.result) {
			case intersect_result::distinct:
				spdlog::debug("{:5}-{:<5} are distinct", hi, lo);
				// and... we're done, next pair please!
				continue;
			case intersect_result::touching:
				spdlog::info("{:5}-{:<5} touch", hi, lo);
				break;
			case intersect_result::overlap: {
				const double
					vol_common = output.vol_common,
					min_vol = std::min(volumes[hi], volumes[lo]),
					max_overlap = min_vol * imprint_max_common_volume_ratio;

				if (vol_common > max_overlap) {
					spdlog::warn(
						"{:5}-{:<5} too much overlap ({:.2f}) between shapes ({:.2f}, {:.2f})",
						hi, lo, vol_common, volumes[hi], volumes[lo]);
				} else {
					spdlog::info(
						"{:5}-{:<5} overlap by an acceptable amount, {:.2f}% of smaller shape",
						hi, lo, vol_common / min_vol * 100);
				}
				break;
			}
			}
		}

		spdlog::debug("joining worker threads");
		for (const auto tid : threads) {
			void *tmp;
			assert(pthread_join(tid, &tmp) == 0);
		}
	}

	return 0;
}
