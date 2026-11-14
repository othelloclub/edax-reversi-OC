#ifndef EDAX_MOVE_ORDERING_WEIGHTS_H
#define EDAX_MOVE_ORDERING_WEIGHTS_H
/* Ordinary default ordering weights. GPL-3.0-or-later. */
enum {
	w_hash = 1 << 15,
	w_eval = 1 << 15,
	w_mobility = 1 << 15,
	w_corner_stability = 1 << 11,
	w_edge_stability = 1 << 11,
	w_potential_mobility = 1 << 5,
	w_low_parity = 1 << 3,
	w_mid_parity = 1 << 2,
	w_high_parity = 1 << 1
};

#endif
