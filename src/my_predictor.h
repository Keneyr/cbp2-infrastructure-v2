// my_predictor.h
// This file contains a sample gshare_predictor class.
// It is a simple 32,768-entry gshare with a history length of 15.

class gshare_update : public branch_update {
public:
	unsigned int index;
};

class gshare_predictor : public branch_predictor {
public:
#define HISTORY_LENGTH	15
#define TABLE_BITS	15
	gshare_update u;
	branch_info bi;
	unsigned int history;
	unsigned char tab[1<<TABLE_BITS];

	gshare_predictor (void) : history(0) { 
		memset (tab, 0, sizeof (tab));
	}

	branch_update *predict (branch_info & b) {
		bi = b;
		if (b.br_flags & BR_CONDITIONAL) {
			u.index = 
				  (history << (TABLE_BITS - HISTORY_LENGTH)) 
				^ (b.address & ((1<<TABLE_BITS)-1));
			u.direction_prediction (tab[u.index] >> 1);
		} else {
			u.direction_prediction (true);
		}
		u.target_prediction (0);
		return &u;
	}

	void update (branch_update *u, bool taken, unsigned int target) {
		if (bi.br_flags & BR_CONDITIONAL) {
			unsigned char *c = &tab[((gshare_update*)u)->index];
			if (taken) {
				if (*c < 3) (*c)++;
			} else {
				if (*c > 0) (*c)--;
			}
			history <<= 1;
			history |= taken;
			history &= (1<<HISTORY_LENGTH)-1;
		}
	}
};

//
// Pentium M hybrid branch predictors
// This class implements a simple hybrid branch predictor based on the Pentium M branch outcome prediction units. 
// Instead of implementing the complete Pentium M branch outcome predictors, the class below implements a hybrid 
// predictor that combines a bimodal predictor and a global predictor. 
class pm_update : public branch_update {
public:
	unsigned int bimodal_index;
	unsigned int global_set;
	unsigned int global_tag;
	unsigned int global_way;
	bool conditional;
	bool global_hit;
};

class pm_predictor : public branch_predictor {
public:
	enum { BIMODAL_SIZE = 1 << 4, GLOBAL_SET_COUNT = 1 << 2,
	       GLOBAL_WAYS = 2, HISTORY_BITS = 4 };
	struct global_entry {
		bool valid;
		unsigned char tag;
		unsigned char counter;
	};

	pm_update u;
	unsigned int history;
	unsigned char bimodal[BIMODAL_SIZE];
	global_entry global[GLOBAL_SET_COUNT][GLOBAL_WAYS];
	unsigned char lru_way[GLOBAL_SET_COUNT];

	pm_predictor (void) : history(0) {
		for (unsigned int i = 0; i < BIMODAL_SIZE; i++)
			bimodal[i] = 1;

		for (unsigned int set = 0; set < GLOBAL_SET_COUNT; set++) {
			lru_way[set] = 0;
			for (unsigned int way = 0; way < GLOBAL_WAYS; way++) {
				global[set][way].valid = false;
				global[set][way].counter = 1;
			}
		}
	}

	branch_update *predict (branch_info & b) {
		u.conditional = (b.br_flags & BR_CONDITIONAL) != 0;
		u.direction_prediction (true);
		u.target_prediction (0);

		if (u.conditional) {
			unsigned int branch_index = (b.address >> 2) & (BIMODAL_SIZE - 1);
			unsigned int global_index = branch_index ^ history;
			u.bimodal_index = branch_index;
			u.global_set = global_index >> 2;
			u.global_tag = global_index & 3;
			u.global_hit = false;

			for (unsigned int way = 0; way < GLOBAL_WAYS; way++) {
				global_entry &entry = global[u.global_set][way];
				if (entry.valid && entry.tag == u.global_tag) {
					u.global_way = way;
					u.global_hit = true;
					u.direction_prediction (entry.counter >= 2);
					break;
				}
			}

			if (!u.global_hit)
				u.direction_prediction (bimodal[u.bimodal_index] >= 2);
		}
		return &u;
	}

	void update (branch_update *update, bool taken, unsigned int target) {
		pm_update *state = (pm_update *) update;
		if (!state->conditional) return;

		train_counter (bimodal[state->bimodal_index], taken);

		unsigned int way = state->global_way;
		if (!state->global_hit) {
			if (!global[state->global_set][0].valid)
				way = 0;
			else if (!global[state->global_set][1].valid)
				way = 1;
			else
				way = lru_way[state->global_set];

			global_entry &entry = global[state->global_set][way];
			entry.valid = true;
			entry.tag = state->global_tag;
			entry.counter = 1;
		}

		train_counter (global[state->global_set][way].counter, taken);
		lru_way[state->global_set] = 1 - way;
		history = ((history << 1) | (taken ? 1U : 0U)) & ((1U << HISTORY_BITS) - 1);
	}

private:
	void train_counter (unsigned char &counter, bool taken) {
		if (taken) {
			if (counter < 3) counter++;
		} else if (counter > 0) {
			counter--;
		}
	}

};

//
// Complete Pentium M branch predictors for extra credit
// This class implements the complete Pentium M branch prediction units. 
// It implements both branch target prediction and branch outcome predicton. 
class cpm_update : public branch_update {
public:
	unsigned int target_index;
	unsigned int branch_address;
};

class cpm_predictor : public branch_predictor {
public:
	enum { TARGET_BITS = 12, TARGET_SIZE = 1 << TARGET_BITS };
	struct target_entry {
		bool valid;
		unsigned int address;
		unsigned int target;
	};

        cpm_update u;
	pm_predictor direction;
	pm_update *direction_update;
	target_entry targets[TARGET_SIZE];

	cpm_predictor (void) : direction_update(0) {
		for (unsigned int i = 0; i < TARGET_SIZE; i++)
			targets[i].valid = false;
        }

        branch_update *predict (branch_info & b) {
		direction_update = (pm_update *) direction.predict (b);
		u.direction_prediction (direction_update->direction_prediction());

		u.target_index = (b.address >> 2) & (TARGET_SIZE - 1);
		u.branch_address = b.address;
		u.target_prediction (0);
		if (targets[u.target_index].valid &&
		    targets[u.target_index].address == b.address)
			u.target_prediction (targets[u.target_index].target);
		return &u;
        }

        void update (branch_update *u, bool taken, unsigned int target) {
		cpm_update *state = (cpm_update *) u;
		direction.update (direction_update, taken, target);

		target_entry &entry = targets[state->target_index];
		entry.valid = true;
		entry.address = state->branch_address;
		entry.target = target;
        }

};

