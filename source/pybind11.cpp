#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "pyinterface.h"

namespace py = pybind11;



PYBIND11_MODULE(yaneuraou, m) {
	py::class_<PyMove>(m, "Move")
		.def_static("make_move", &PyMove::make_move)
		.def_static("make_move_promote", &PyMove::make_move_promote)
		.def_static("make_move_drop", &PyMove::make_move_drop)
		.def_static("from_usi", &PyMove::from_usi)
		.def_static("from_int", &PyMove::from_int)
		.def_readonly_static("MOVE_NONE", &PyMove::MOVE_NONE)
		.def_readonly_static("MOVE_NULL", &PyMove::MOVE_NULL)
		.def_readonly_static("MOVE_RESIGN", &PyMove::MOVE_RESIGN)
		.def_readonly_static("MOVE_WIN", &PyMove::MOVE_WIN)
		.def("is_ok", &PyMove::is_ok)
		.def("is_drop", &PyMove::is_drop)
		.def("is_promote", &PyMove::is_promote)
		.def("special", &PyMove::special)
		.def("move_from", &PyMove::move_from)
		.def("move_to", &PyMove::move_to)
		.def("move_dropped_piece", &PyMove::move_dropped_piece)
		.def("to_usi_string", &PyMove::to_usi_string)
		.def("to_int", &PyMove::to_int)
		.def("__str__", &PyMove::to_usi_string)
		.def("__repr__", &PyMove::to_usi_string)
		.def("__eq__", &PyMove::__eq__)
		;

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
		.def("legal", &PyPosition::legal)
		.def("search", &PyPosition::search)
		.def("get_board", &PyPosition::get_board)
		.def("get_hand", &PyPosition::get_hand)
		.def("generate_move_list", &PyPosition::genereate_move_list)
		.def("set_from_packed_sfen", &PyPosition::set_from_packed_sfen)
		.def("set_from_packed_sfen_value", &PyPosition::set_from_packed_sfen_value)
		.def("sfen_pack", &PyPosition::sfen_pack)
		.def("__str__", &PyPosition::sfen)
		.def("__repr__", &PyPosition::sfen)
		.def_readonly("psv_score", &PyPosition::psv_score)
		.def_readonly("psv_move", &PyPosition::psv_move)
		.def_readonly("psv_game_ply", &PyPosition::psv_game_ply)
		.def_readonly("psv_game_result", &PyPosition::psv_game_result)
		;

	py::class_<DNNConverter>(m, "DNNConverter")
		.def(py::init<int, int>())
		.def("board_shape", &DNNConverter::board_shape)
		.def("move_shape", &DNNConverter::move_shape)
		.def("get_board_array", &DNNConverter::get_board_array)
		.def("get_move_array", &DNNConverter::get_move_array)
		.def("get_legal_move_array", &DNNConverter::get_legal_move_array)
		.def("get_move_index", &DNNConverter::get_move_index)
		.def("reverse_move_index", &DNNConverter::reverse_move_index)
		;
}
