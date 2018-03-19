// pyshogieval.cpp : DLL アプリケーション用にエクスポートされる関数を定義します。
//

#include "stdafx.h"

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include "shogieval.h"

PYBIND11_MODULE(pyshogieval, m) {
	m.doc() = "Inter-process communication module for neneshogi evaluation";

	py::class_<ShogiEval>(m, "ShogiEval")
		.def(py::init<>())
		.def("get", &ShogiEval::get)
		.def("put", &ShogiEval::put)
		;
}
