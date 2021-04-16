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

#include <iostream>
#include "sequence_file.h"
#include "../basic/masking.h"
#include "reference.h"
#include "dmnd/dmnd.h"
#include "../util/system/system.h"
#ifdef WITH_BLASTDB
#include "blastdb/blastdb.h"
#endif

using std::cout;
using std::endl;
using std::setw;

const EMap<SequenceFile::Type> EnumTraits<SequenceFile::Type>::to_string = { {SequenceFile::Type::DMND, "Diamond database" }, {SequenceFile::Type::BLAST, "BLAST database"} };

Block* SequenceFile::load_seqs(const size_t max_letters, bool load_ids, const BitVector* filter, const bool fetch_seqs, const Chunk& chunk)
{
	task_timer timer("Loading reference sequences");
	reopen();

	if (max_letters > 0)
		init_seqinfo_access();
	else
		seek_chunk(chunk);

	size_t database_id = tell_seq();
	size_t letters = 0, seqs = 0, id_letters = 0, seqs_processed = 0, filtered_seq_count = 0;
	vector<uint64_t> filtered_pos;
	Block* block = new Block();
	
	SeqInfo r = read_seqinfo();
	uint64_t start_offset = r.pos;
	bool last = false;
	if (type() == Type::BLAST && sequence_count() != sparse_sequence_count())
		filter = builtin_filter();
	const bool use_filter = filter && !filter->empty();

	auto goon = [&]() {
		if (max_letters > 0)
			return (r.seq_len > 0 && letters < max_letters);
		else
			return (seqs < chunk.n_seqs);
	};

	while (goon()) {
		SeqInfo r_next = read_seqinfo();
		if (!use_filter || filter->get(database_id)) {
			letters += r.seq_len;
			if (fetch_seqs) {
				block->seqs_.reserve(r.seq_len);
			}
			if (load_ids) {
				const size_t id_len = this->id_len(r, r_next);
				id_letters += id_len;
				if (fetch_seqs)
					block->ids_.reserve(id_len);
			}
			++filtered_seq_count;
			block->block2oid_.push_back((unsigned)database_id);
			if (use_filter) {
				filtered_pos.push_back(last ? 0 : r.pos);
			}
			last = true;
		}
		else {
			last = false;
		}
		++database_id;
		++seqs_processed;
		r = r_next;
		++seqs;
	}

	putback_seqinfo();

	if (seqs == 0 || filtered_seq_count == 0)
		return block;

	if (fetch_seqs) {
		block->seqs_.finish_reserve();
		if (load_ids) block->ids_.finish_reserve();
		seek_offset(start_offset);

		for (size_t i = 0; i < filtered_seq_count; ++i) {
			if (use_filter && filtered_pos[i]) seek_offset(filtered_pos[i]);
			read_seq_data(block->seqs_.ptr(i), block->seqs_.length(i));
			if (load_ids)
				read_id_data(block->ids_.ptr(i), block->ids_.length(i));
			else
				skip_id_data();
			Masking::get().remove_bit_mask(block->seqs_.ptr(i), block->seqs_.length(i));
		}
		timer.finish();
		block->seqs_.print_stats();
	}

	if (config.multiprocessing || config.global_ranking_targets)
		blocked_processing = true;
	else
		blocked_processing = seqs_processed < sequence_count();

	if (blocked_processing)
		close_weakly();
	return block;
}

void SequenceFile::get_seq()
{
	std::map<string, string> seq_titles;
	if (!config.query_file.empty()) {
		TextInputFile list(config.single_query_file());
		while (list.getline(), !list.eof()) {
			const vector<string> t(tokenize(list.line.c_str(), "\t"));
			if (t.size() != 2)
				throw std::runtime_error("Query file format error.");
			seq_titles[t[0]] = t[1];
		}
		list.close();
	}

	vector<Letter> seq;
	string id;
	bool all = config.seq_no.size() == 0 && seq_titles.empty();
	std::set<size_t> seqs;
	if (!all)
		for (vector<string>::const_iterator i = config.seq_no.begin(); i != config.seq_no.end(); ++i)
			seqs.insert(atoi(i->c_str()) - 1);
	const size_t max_letters = config.chunk_size == 0.0 ? std::numeric_limits<size_t>::max() : (size_t)(config.chunk_size * 1e9);
	size_t letters = 0;
	TextBuffer buf;
	OutputFile out(config.output_file);
	for (size_t n = 0; n < sequence_count(); ++n) {
		read_seq(seq, id);
		std::map<string, string>::const_iterator mapped_title = seq_titles.find(blast_id(id));
		if (all || seqs.find(n) != seqs.end() || mapped_title != seq_titles.end()) {
			buf << '>' << (mapped_title != seq_titles.end() ? mapped_title->second : id) << '\n';
			if (config.reverse) {
				Sequence(seq).print(buf, value_traits, Sequence::Reversed());
				buf << '\n';
			}
			else if (config.hardmasked) {
				Sequence(seq).print(buf, value_traits, Sequence::Hardmasked());
				buf << '\n';
			}
			else
				buf << Sequence(seq) << '\n';
		}
		out.write(buf.get_begin(), buf.size());
		letters += seq.size();
		if (letters >= max_letters)
			break;
		seq.clear();
		id.clear();
		buf.clear();
	}

	out.close();
}

SequenceFile::~SequenceFile()
{
}

SequenceFile* SequenceFile::auto_create(Flags flags, Metadata metadata) {
	if (exists(config.database + ".pin") || exists(config.database + ".pal")) {
#ifdef WITH_BLASTDB
		if (config.multiprocessing)
			throw std::runtime_error("--multiprocessing is not compatible with BLAST databases.");
		if (config.target_indexed)
			throw std::runtime_error("--target-indexed is not compatible with BLAST databases.");
		if (config.global_ranking_targets)
			throw std::runtime_error("");
		return new BlastDB(config.database, metadata, flags);
#else
		throw std::runtime_error("This executable was not compiled with support for BLAST databases.");
#endif
	}
	config.database = auto_append_extension_if_exists(config.database, DatabaseFile::FILE_EXTENSION);
	if (DatabaseFile::is_diamond_db(config.database)) {
		return new DatabaseFile(config.database, metadata, flags);
	}
	else if (!flag_any(flags, Flags::NO_FASTA)) {
		message_stream << "Database file is not a DIAMOND or BLAST database, treating as FASTA." << std::endl;
		config.input_ref_file = { config.database };
		TempFile* db;
		DatabaseFile::make_db(&db);
		DatabaseFile* r(new DatabaseFile(*db));
		delete db;
		return r;
	}
	throw std::runtime_error("Database does not have a supported format.");
}

size_t SequenceFile::total_blocks() const {
	const size_t c = config.chunk_size * 1e9;
	return (this->letters() + c - 1) / c;
}

SequenceFile::SequenceFile(Type type):
	type_(type)
{}

void db_info() {
	if (config.database.empty())
		throw std::runtime_error("Missing option for database file: --db/-d.");
	SequenceFile* db = SequenceFile::auto_create(SequenceFile::Flags::NO_FASTA | SequenceFile::Flags::NO_COMPATIBILITY_CHECK);
	const std::streamsize w = 25;
	cout << setw(w) << "Database type  " << to_string(db->type()) << endl;
	cout << setw(w) << "Database format version  " << db->db_version() << endl;
	if(db->type() == SequenceFile::Type::DMND)
		cout << setw(w) << "Diamond build  " << db->program_build_version() << endl;
	cout << setw(w) << "Sequences  " << db->sequence_count() << endl;
	if (db->type() == SequenceFile::Type::BLAST && db->sequence_count() != db->sparse_sequence_count())
		cout << setw(w) << "Sequences (filtered) " << db->sparse_sequence_count() << endl;
	cout << setw(w) << "Letters  " << db->letters() << endl;
	db->close();
	delete db;
}