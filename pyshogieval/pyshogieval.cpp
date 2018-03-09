﻿// pyshogieval.cpp : DLL アプリケーション用にエクスポートされる関数を定義します。
//

#include "stdafx.h"

#include <pybind11/pybind11.h>

int add(int i, int j) {
	return i + j;
}

PYBIND11_MODULE(pyshogieval, m) {
	m.doc() = "Inter-process communication module for neneshogi evaluation";

	m.def("add", &add, "A function which adds two numbers");
}
