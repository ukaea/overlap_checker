#include <string>
#include <vector>

#include <thread>
#include <deque>
#include <mutex>
#include <condition_variable>

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <CLI/App.hpp>
#include <CLI/Formatter.hpp>
#include "CLI/Validators.hpp"
#include <CLI/Config.hpp>

#include <BRepBndLib.hxx>
#include <Bnd_OBB.hxx>

#include "document.hpp"
#include "utils.hpp"


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
shape_classifier(
	const document &doc, worker_queue &queue, std::vector<double> &fuzzy_values)
{
	spdlog::debug("worker thread starting");

	const auto &shapes = doc.solid_shapes;

	worker_input input;
	worker_output output;

	while (queue.next_input(input)) {
		const auto &shape = shapes[output.hi = input.hi];
		const auto &tool = shapes[output.lo = input.lo];

		bool first = true;
		for (const auto fuzzy_value : fuzzy_values) {
			if (!first) {
				spdlog::info(
					"{:5}-{:<5} imprint failed with ({} filler and {} common) warnings, retrying with tolerance={}",
					input.hi, input.lo,
					output.result.num_filler_warnings, output.result.num_common_warnings,
					fuzzy_value);
			}

			output.result = classify_solid_intersection(
				shape, tool, fuzzy_value);

			// try again with less fuzz
			if (output.result.status != intersect_status::failed) {
				break;
			}
		}

		if (output.result.status == intersect_status::failed) {
			spdlog::warn(
				"{:5}-{:<5} imprint failed with ({} filler and {} common) warnings",
				input.hi, input.lo,
				output.result.num_filler_warnings, output.result.num_common_warnings);
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

	std::string path_in;
	unsigned num_parallel_jobs;
	double
		bbox_clearance = 0.5,
		max_common_volume_ratio = 0.01;
	std::vector<double> imprint_tolerances = {0.001, 0};

	{
		CLI::App app{"Perform imprinting of BREP shapes."};
		app.add_option(
			"input", path_in,
			"Path of the input file")
			->required()
			->option_text("file.brep");
		app.add_option(
			"-j", num_parallel_jobs,
			"Parallelise over N threads")
			->option_text("N")
			->default_val(4)
			->check(CLI::Range(1, 1024));
		app.add_option(
			"--bbox-clearance", bbox_clearance,
			"Bounding-boxes closer than C will be checked for overlaps")
			->option_text("C");
		app.add_option(
			"--imprint-tolerance", imprint_tolerances,
			"Faces, edges, and verticies will be merged when closer than T")
			->option_text("T")
			->expected(1, 10);
		app.add_option(
			"--max-common-volume-ratio", max_common_volume_ratio,
			"Imprinted volume with ratio <R is considered acceptable")
			->option_text("R")
			->check(CLI::Range(0., 1.));
		CLI11_PARSE(app, argc, argv);
	}

	// make sure parameters are sane!
	{
		unsigned max_parallel = std::thread::hardware_concurrency();
		if (max_parallel && num_parallel_jobs > max_parallel * 2) {
			spdlog::warn(
				"requesting significantly more than the number of cores ({} > {}) is unlikely to help",
				num_parallel_jobs, max_parallel);
		}
	}

	if (bbox_clearance < 0) {
		spdlog::critical(
			"bounding-box clearance ({}) should not be negative",
			bbox_clearance);
		return 1;
	}

	for (const auto tolerance : imprint_tolerances) {
		if (tolerance < 0) {
			spdlog::critical(
				"imprinting tolerance should not be negative, {} < 0",
				tolerance);
			return 1;
		}

		if (bbox_clearance < tolerance) {
			spdlog::warn(
				"bbox clearance smaller than imprinting tolerance, {} < {}",
				bbox_clearance, tolerance);
		}
	}

	document doc;
	doc.load_brep_file(path_in.c_str());

	spdlog::debug("calculating bounding boxes");
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

	spdlog::debug("starting imprinting");

	worker_queue queue;

	for (size_t hi = 1; hi < doc.solid_shapes.size(); hi++) {
		for (size_t lo = 0; lo < hi; lo++) {
			// seems reasonable to assume majority of shapes aren't close to
			// overlapping, so check with coarser limit first
			if (are_bboxs_disjoint(
					bounding_boxes[hi], bounding_boxes[lo], bbox_clearance)) {
				continue;
			}

			worker_input work{hi, lo};
			queue.add_work(work);
		}
	}

	{
		size_t
			remain = queue.input_size(),
			num_failed = 0,
			num_intersected = 0;

		spdlog::debug("launching {} worker threads", num_parallel_jobs);
		std::vector<std::thread> threads;
		for (unsigned i = 0; i < num_parallel_jobs; i++) {
			threads.emplace_back(shape_classifier, std::ref(doc), std::ref(queue), std::ref(imprint_tolerances));
		}
		spdlog::debug("waiting for results from workers");

		while (remain--) {
			worker_output output = queue.next_output();

			size_t hi = output.hi, lo = output.lo;

			switch (output.result.status) {
			case intersect_status::failed:
				spdlog::error("{:5}-{:<5} failed to classify overlap");
				num_failed += 1;
				break;
			case intersect_status::distinct:
				spdlog::debug("{:5}-{:<5} are distinct", hi, lo);
				break;
			case intersect_status::touching:
				fmt::print("{},{},touch\n", hi, lo);
				break;
			case intersect_status::overlap: {
				const double
					vol_common = output.result.vol_common,
					min_vol = std::min(volumes[hi], volumes[lo]),
					max_overlap = min_vol * max_common_volume_ratio;

				if (vol_common > max_overlap) {
					spdlog::error(
						"{:5}-{:<5} overlap by more than {}%, {:.2f}% of smaller shape",
						hi, lo, max_common_volume_ratio * 100, vol_common / min_vol * 100);
					fmt::print("{},{},bad_overlap\n", hi, lo);
					num_intersected += 1;
				} else {
					spdlog::info(
						"{:5}-{:<5} overlap by less than {}%, {:.2f}% of smaller shape",
						hi, lo, max_common_volume_ratio * 100, vol_common / min_vol * 100);
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
			spdlog::error(
				"errors occurred while processing, {} failed, {} intersected",
				num_failed, num_intersected);
			return 1;
		}
	}

	return 0;
}
