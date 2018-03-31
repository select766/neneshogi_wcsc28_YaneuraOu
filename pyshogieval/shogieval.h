#pragma once

#include "pyshogieval_config.h"
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include "../ipqueue/ipqueue/ipqueue.h"
#include "../source/engine/user-engine/dnn_eval_obj.h"

namespace py = pybind11;

class ShogiEval
{
	ipqueue<dnn_eval_obj> *eval_queue;
	ipqueue<dnn_result_obj> *result_queue;
public:
	static const int MOVE_SIZE = MAX_MOVES;
	static const int DNN_INPUT_CHANNEL = 86;
	ShogiEval(int size, int batch_sizee, std::string name_prefix);
	py::tuple get(py::array_t<float, py::array::c_style> dnn_input, py::array_t<uint16_t, py::array::c_style> move_and_index,
		py::array_t<uint16_t, py::array::c_style> n_moves);
	void put(int count, std::string dnn_table_indexes, py::array_t<uint16_t, py::array::c_style> move_and_prob,
		py::array_t<uint16_t, py::array::c_style> n_moves, py::array_t<int16_t, py::array::c_style> static_value);
};
