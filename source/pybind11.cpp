#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "pyinterface.h"

namespace py = pybind11;



PYBIND11_MODULE(yaneuraou, m) {
	py::class_<PyMove>(m, "Move")
		.def_static("make_move", &PyMove::make_move)
		.def("__str__", &PyMove::to_usi_string);

	py::class_<PyPosition>(m, "Position")
		.def(py::init<>())
		.def("set", &PyPosition::set)
		.def("set_hirate", &PyPosition::set_hirate)
		.def("sfen", &PyPosition::sfen)
		.def("side_to_move", &PyPosition::side_to_move)
		.def("game_ply", &PyPosition::game_ply)
		.def("do_move", &PyPosition::do_move)
		.def("undo_move", &PyPosition::undo_move)
		.def("do_null_move", &PyPosition::do_null_move)
		.def("undo_null_move", &PyPosition::undo_null_move)
		.def("key", &PyPosition::key)
		.def("in_check", &PyPosition::in_check)
		.def("is_mated", &PyPosition::is_mated)
		.def("get_board", &PyPosition::get_board)
		.def("get_hand", &PyPosition::get_hand)
		.def("generate_move_list", &PyPosition::genereate_move_list)
		.def("__str__", &PyPosition::sfen)
		;
}
