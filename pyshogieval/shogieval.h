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
	ShogiEval();
	int get(py::array_t<float, py::array::c_style> dnn_input, py::array_t<uint16_t, py::array::c_style> move_and_index);
	void put(int n_moves, py::array_t<uint16_t, py::array::c_style> move_and_prob, int16_t static_value);
};
