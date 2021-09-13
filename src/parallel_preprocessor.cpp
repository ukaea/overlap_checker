#include <string>
#include <vector>

#include <thread>
#include <deque>
#include <mutex>
#include <condition_variable>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <CLI/App.hpp>
#include <CLI/Formatter.hpp>
#include <CLI/Config.hpp>

#include <BRepBndLib.hxx>
#include <Bnd_OBB.hxx>

#include "document.hpp"


// helper methods to allow fmt to display some OCCT values nicely
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

struct worker_input {
	size_t hi, lo;
	double fuzzy_value;
};

struct worker_output {
	size_t hi, lo;
	intersect_result result;
};

class worker_queue {
	std::mutex mutex;
	std::condition_variable cond;

	std::deque<worker_input> input;
	std::deque<worker_output> output;

public:
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

static void
shape_classifier(const document &doc, worker_queue &queue)
{
	spdlog::debug("worker thread starting");

	const auto &shapes = doc.solid_shapes;

	worker_input input;
	worker_output output;

	while (queue.next_input(input)) {
		const auto &shape = shapes[output.hi = input.hi];
		const auto &tool = shapes[output.lo = input.lo];

		output.result = classify_solid_intersection(
			shape, tool, input.fuzzy_value);

		// try again with less fuzz
		if (output.result.status == intersect_status::failed) {
			spdlog::info(
				"{:5}-{:<5} merge failed with ({} filler and {} common) warnings, retrying with less fuzzyness",
				input.hi, input.lo, output.result.num_filler_warnings, output.result.num_common_warnings);
			output.result = classify_solid_intersection(shape, tool, 0);
		}

		queue.add_output(output);
	}

	spdlog::debug("worker thread exiting");
}


// "OBB" stands for "orientated bounding-box", i.e. aligned to the shape
// rather than the axis
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

int
main(int argc, char **argv)
{
	configure_spdlog();

	CLI::App app{"Perform inprinting of BREP shapes."};
	std::string path_in;
	unsigned num_parallel = 4;
	bool perform_geometry_checks{true};
	double
		bbox_clearance = 0.5,
		imprint_tolerance = 0.001,
		max_common_volume_ratio = 0.01;

	app.add_option(
		"input", path_in,
		"Path of the input file")
		->required()
		->option_text("file.brep");
	app.add_option(
		"-j", num_parallel,
		"Number of threads to parallelise over")
		->option_text("jobs");
	app.add_flag(
		"--check-geometry,!--no-check-geometry",
		perform_geometry_checks,
		"Check overall validity of shapes");
	app.add_option(
		"--bbox-clearance", bbox_clearance,
		"Bounding-boxes closer than this will be checked for overlaps");
	app.add_option(
		"--imprint-tolerance", imprint_tolerance,
		"Faces, edges, and verticies will be merged when closer than this");

	CLI11_PARSE(app, argc, argv);

	// make sure parameters are sane!
	if (num_parallel > 1024) {
		spdlog::critical("using >1024 threads is currently unsupported");
		return 1;
	} else if (num_parallel == 0) {
		// might want to properly handle this by not using threads, but this
		// is enough to get it working
		num_parallel = 1;
	} else {
		unsigned max_parallel = std::thread::hardware_concurrency();
		if (max_parallel && num_parallel > max_parallel * 2) {
			spdlog::warn(
				"requesting significantly more than the number of cores ({} > {}) is unlikely to help",
				num_parallel, max_parallel);
		}
	}

	if (bbox_clearance < 0) {
		spdlog::critical(
			"bounding-box clearance ({}) should not be negative",
			bbox_clearance);
		return 1;
	}

	if (imprint_tolerance < 0) {
		spdlog::critical(
			"imprinting tolerance ({}) should not be negative",
			imprint_tolerance);
		return 1;
	}

	if (bbox_clearance < imprint_tolerance) {
		spdlog::warn(
			"bbox clearance ({}) smaller than imprinting tolerance ({})",
			bbox_clearance, imprint_tolerance);
	}

	if (max_common_volume_ratio < 0 ||
		max_common_volume_ratio > 1) {
		spdlog::critical(
			"max common volume ratio ({}) should be in (0., 1.) when inprinting",
			max_common_volume_ratio);
		return 1;
	}

	document doc;
	doc.load_brep_file(path_in.c_str());

	if (perform_geometry_checks) {
		spdlog::debug("checking geometry");
		auto ninvalid = doc.count_invalid_shapes();
		if (ninvalid) {
			spdlog::critical("{} shapes were not valid", ninvalid);
			return 1;
		}
	}

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

	spdlog::info("starting imprinting");

	worker_queue queue;

	for (size_t hi = 1; hi < doc.solid_shapes.size(); hi++) {
		for (size_t lo = 0; lo < hi; lo++) {
			// seems reasonable to assume majority of shapes aren't close to
			// overlapping, so check with coarser limit first
			if (are_bboxs_disjoint(
					bounding_boxes[hi], bounding_boxes[lo], bbox_clearance)) {
				continue;
			}

			worker_input work{hi, lo, imprint_tolerance};
			queue.add_work(work);
		}
	}

	{
		size_t
			remain = queue.input_size(),
			num_failed = 0,
			num_intersected = 0;

		spdlog::debug("launching worker threads");
		std::vector<std::thread> threads;
		for (unsigned i = 0; i < num_parallel; i++) {
			threads.emplace_back(shape_classifier, std::ref(doc), std::ref(queue));
		}
		spdlog::debug("waiting for results from workers");

		while (remain--) {
			worker_output output = queue.next_output();

			size_t hi = output.hi, lo = output.lo;

			switch (output.result.status) {
			case intersect_status::failed:
				spdlog::warn("{:5}-{:<5} failed to classify overlap");
				num_failed += 1;
				continue;
			case intersect_status::distinct:
				spdlog::debug("{:5}-{:<5} are distinct", hi, lo);
				continue;
			case intersect_status::touching:
				fmt::print("{},{},touch\n", hi, lo);
				break;
			case intersect_status::overlap: {
				const double
					vol_common = output.result.vol_common,
					min_vol = std::min(volumes[hi], volumes[lo]),
					max_overlap = min_vol * max_common_volume_ratio;

				if (vol_common > max_overlap) {
					spdlog::warn(
						"{:5}-{:<5} too much overlap ({:.2f}) between shapes ({:.2f}, {:.2f})",
						hi, lo, vol_common, volumes[hi], volumes[lo]);
					fmt::print("{},{},bad_overlap\n", hi, lo);
					num_intersected += 1;
				} else {
					spdlog::info(
						"{:5}-{:<5} overlap by an acceptable amount, {:.2f}% of smaller shape",
						hi, lo, vol_common / min_vol * 100);
					fmt::print("{},{},overlap\n", hi, lo);
				}
				break;
			}
			}

			// flush any CSV output
			std::cout << std::flush;
		}

		spdlog::debug("joining worker threads");
		for (auto &thread : threads) {
			thread.join();
		}

		if (num_failed || num_intersected) {
			spdlog::critical(
				"errors occurred while processing, {} failed, {} intersected",
				num_failed, num_intersected);
			return 1;
		}
	}

	return 0;
}
