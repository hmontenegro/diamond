/****
DIAMOND protein aligner
Copyright (C) 2013-2021 Max Planck Society for the Advancement of Science e.V.
                        Benjamin Buchfink
                        Eberhard Karls Universitaet Tuebingen
						
Code developed by Benjamin Buchfink <benjamin.buchfink@tue.mpg.de>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
****/

#pragma once
#include <stdint.h>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <string>
#include "../util/ptr_vector.h"
#include "../util/io/output_file.h"
#include "reference.h"
#include "../run/workflow.h"

struct ReferenceDictionary
{

	ReferenceDictionary() :
		next_(0)
	{ }

	void init(unsigned ref_count, const vector<unsigned> &block_to_database_id);

	uint32_t get(unsigned block, size_t i, const Search::Config& cfg);
	void build_lazy_dict(SequenceFile &db_file, Search::Config& cfg);
	void clear();

	unsigned length(uint32_t i) const
	{
		return config.no_dict ? 1 : len_[i];
	}

	const char* name(uint32_t i) const
	{
		return config.no_dict ? "" : name_[i].c_str();
	}

	size_t dict_to_lazy_dict_id(size_t i) const {
		return dict_to_lazy_dict_id_[i];
	}

	//Sequence seq(size_t i) const
	//{
	//	return ref_seqs::get()[dict_to_lazy_dict_id_[i]];
	//}

	//void init_rev_map();

	uint32_t database_id(unsigned dict_id) const
	{
		return config.no_dict ? 0 : database_id_[dict_id];
	}

	unsigned block_to_database_id(size_t block_id) const
	{
		return (*block_to_database_id_)[block_id];
	}

	void set_block2db(const vector<unsigned>* block_to_database_id) {
		block_to_database_id_ = block_to_database_id;
	}

	uint32_t check_id(uint32_t i) const
	{
		if (i >= next_)
			throw std::runtime_error("Dictionary reference id out of bounds.");
		return i;
	}

	static ReferenceDictionary& get()
	{
		return instance_;
	}

	static ReferenceDictionary& get(int block)
	{
		return block_instances_[block];
	}

	uint32_t seqs() const
	{
		return next_;
	}

	void clear_block(size_t block);
	void clear_block_instances();

	void save_block(size_t query, size_t block);
	void load_block(size_t query, size_t block, ReferenceDictionary & d);
	void restore_blocks(size_t query, size_t n_blocks);
	void remove_temporary_files(size_t query, size_t n_blocks);

private:

	static ReferenceDictionary instance_;
	static std::unordered_map<size_t,ReferenceDictionary> block_instances_;

	std::mutex mtx_;

	std::vector<vector<uint32_t>> data_;
	// vector<vector<uint32_t>> init_data_;

	std::vector<uint32_t> len_, database_id_;
	PtrVector<string> name_;
	//vector<uint32_t> rev_map_;
	uint32_t next_;
	std::vector<uint32_t> dict_to_lazy_dict_id_;
	const std::vector<unsigned> *block_to_database_id_;

	friend void finish_daa(OutputFile&, const SequenceFile&);

};
