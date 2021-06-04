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

#include <string>
// #include <sstream>
// #include <iomanip>
#include <cstdio>
#include <utility>
#include "reference.h"
#include "ref_dictionary.h"
#include "../util/util.h"
#include "../util/parallel/multiprocessing.h"
#include "../util/sequence/sequence.h"

using std::pair;

ReferenceDictionary ReferenceDictionary::instance_;
std::unordered_map<size_t,ReferenceDictionary> ReferenceDictionary::block_instances_;

void ReferenceDictionary::clear()
{
	data_.clear();
	len_.clear();
	database_id_.clear();
	name_.clear();
	next_ = 0;
}

void ReferenceDictionary::clear_block_instances()
{
	for (std::unordered_map<size_t,ReferenceDictionary>::iterator it = block_instances_.begin(); it != block_instances_.end(); it++) {
		it->second.clear();
	}
	block_instances_.clear();
}

void ReferenceDictionary::init(unsigned ref_count, const vector<unsigned> &block_to_database_id)
{
	const unsigned block = current_ref_block;
	if (data_.size() < block + 1) {
		data_.resize(block + 1);
		data_[block].insert(data_[block].end(), ref_count, std::numeric_limits<uint32_t>::max());
	}
	block_to_database_id_ = &block_to_database_id;
}

uint32_t ReferenceDictionary::get(unsigned block, size_t block_id, const Search::Config& cfg)
{
	uint32_t n = data_[block][block_id];
	if (n != std::numeric_limits<uint32_t>::max())
		return n;
	{
		mtx_.lock();
		n = data_[block][block_id];
		if (n != std::numeric_limits<uint32_t>::max()) {
			mtx_.unlock();
			return n;
		}
		n = next_++;
		data_[block][block_id] = n;
		if (!config.no_dict) {
			len_.push_back((uint32_t)cfg.target->seqs().length(block_id));
			database_id_.push_back((*block_to_database_id_)[block_id]);
			const char *title = cfg.target->ids()[block_id];
			/*if (config.salltitles)
				name_.push_back(new string(title));
			else if (config.sallseqid)
				name_.push_back(get_allseqids(title));
			else
				name_.push_back(Util::Seq::seqid(title));*/
		}
		mtx_.unlock();
	}
	return n;
}

void ReferenceDictionary::build_lazy_dict(SequenceFile &db_file, Search::Config& cfg)
{
	BitVector filter(db_file.sequence_count());
	vector<pair<unsigned, unsigned> > m;
	const size_t dict_size = database_id_.size();
	m.reserve(dict_size);
	unsigned n = 0;
	for (vector<uint32_t>::const_iterator i = database_id_.begin(); i < database_id_.end(); ++i) {
		filter.set(*i);
		m.push_back(std::make_pair(*i, n++));
	}
	db_file.set_seqinfo_ptr(0);
	cfg.target.reset(db_file.load_seqs(std::numeric_limits<size_t>::max(), false, &filter));
	std::sort(m.begin(), m.end());
	dict_to_lazy_dict_id_.clear();
	dict_to_lazy_dict_id_.resize(dict_size);
	n = 0;
	for (vector<pair<unsigned, unsigned> >::const_iterator i = m.begin(); i < m.end(); ++i)
		dict_to_lazy_dict_id_[i->second] = n++;
}

/*void ReferenceDictionary::init_rev_map()
{
	rev_map_.resize(next_);
	unsigned n = 0;
	for (unsigned i = 0; i < data_.size(); ++i) {
		for (unsigned j = 0; j < data_[i].size(); ++j)
			if (data_[i][j] != std::numeric_limits<uint32_t>::max())
				rev_map_[data_[i][j]] = n + j;
		n += (unsigned)data_[i].size();
	}
}
*/

string _get_file_name(size_t query, size_t block) {
	const string file_name = append_label("ref_dict_", query) + append_label("_", block);
	return join_path(config.parallel_tmpdir, file_name);
}

void ReferenceDictionary::save_block(size_t query, size_t block) {
	const string o_file = _get_file_name(query, block);
	std::ofstream os(o_file, std::ios::out | std::ios::trunc | std::ios::binary);
	save_scalar(os, next_);
	save_vector(os, len_);
	save_vector(os, database_id_);
	size_t sz = name_.size();
	save_scalar(os, sz);
	for (auto i = name_.begin(); i < name_.end(); ++i) {
		save_vector(os, **i);
	}
}

void ReferenceDictionary::load_block(size_t query, size_t block, ReferenceDictionary & d) {
	const string i_file = _get_file_name(query, block);
	std::ifstream is(i_file, std::ios::in | std::ios::binary);
	load_scalar(is, d.next_);
	load_vector(is, d.len_);
	load_vector(is, d.database_id_);
	size_t sz;
	load_scalar(is, sz);
	for (size_t i = 0; i < sz; ++i) {
		string * buf = new string();
		load_string(is, *buf);
		d.name_.push_back(buf);
	}
	is.close();
	// std::remove(i_file.c_str());
}

void ReferenceDictionary::restore_blocks(size_t query, size_t n_blocks) {
	for (size_t i = 0; i < n_blocks; ++i) {
		ReferenceDictionary & d = block_instances_[i];
		load_block(query, i, d);
	}
}

void ReferenceDictionary::clear_block(size_t block) {
	len_.clear();
	name_.clear();
	database_id_.clear();
	data_[block].clear();
	next_ = 0;
}

void ReferenceDictionary::remove_temporary_files(size_t query, size_t n_blocks) {
	for (size_t i = 0; i < n_blocks; ++i) {
		const string i_file = _get_file_name(query, i);
		std::remove(i_file.c_str());
	}
}
