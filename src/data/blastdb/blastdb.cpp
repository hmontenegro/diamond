/****
DIAMOND protein aligner
Copyright (C) 2021 Max Planck Society for the Advancement of Science e.V.

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

#include <objmgr/object_manager.hpp>
#include <objmgr/scope.hpp>
#include <objmgr/util/create_defline.hpp>
#include <objtools/blast/blastdb_format/blastdb_dataextract.hpp>
#include <corelib/ncbiutil.hpp>
#include "../util/io/text_input_file.h"
#include "blastdb.h"
#include "../../util/string/tokenizer.h"

using std::cout;
using std::endl;
using std::vector;
using namespace ncbi;

static string full_id(CBioseq& bioseq, CBioseq_Handle* bioseq_handle, bool long_ids, bool ctrl_a) {
	string id;
	if (long_ids) {
		CConstRef<CSeq_id> best_id = FindBestChoice(bioseq.GetId(), CSeq_id::FastaAARank);
		id = best_id->AsFastaString();
		sequence::CDeflineGenerator gen;
		id += gen.GenerateDefline(*bioseq_handle, 0);
	}
	else {
		CBlastDeflineUtil::ProcessFastaDeflines(bioseq, id, ctrl_a);
		id.erase(0, 1);
		id.pop_back();
	}
	return id;
}

template<typename _it>
static void load_seq_data(CBioseq& bioseq, CBioseq_Handle bioseq_handle, _it it) {
	ncbi::objects::CSeqVector v = bioseq_handle.GetSeqVector(CBioseq_Handle::eCoding_Iupac);
	if (v.GetCoding() != CSeq_data::e_Iupacaa)
		throw std::runtime_error("Invalid sequence coding in BLAST database.");
	
	for (size_t i = 0; i < v.size(); ++i) {
		const auto l = v[i] & 31;
		const Letter s = IUPACAA_TO_STD[l];
		if (s == -1)
			throw std::runtime_error("Unrecognized sequence character in BLAST database letter=" + std::to_string(l)
				+ " accession=" + bioseq.GetFirstId()->AsFastaString()
				+ " position=" + std::to_string(i + 1));
		*it = s;
		++it;
	}
}

string best_id(const list<CRef<CSeq_id>>& ids) {
	if (ids.empty())
		throw std::runtime_error("Unable to retrieve sequence id from BLAST database.");
	auto min = ids.cbegin(), it = min;
	int min_score = (*min)->TextScore(), s;
	++it;
	while (it != ids.cend()) {
		if ((s = (*it)->TextScore()) < min_score) {
			min_score = s;
			min = it;
		}
		++it;
	}
	return (*min)->GetSeqIdString();
}

BlastDB::BlastDB(const std::string& file_name, Metadata metadata, Flags flags) :
	SequenceFile(Type::BLAST, Alphabet::NCBI),
	file_name_(file_name),	
	db_(new CSeqDBExpert(file_name, CSeqDB::eProtein)),
	oid_(0),
	long_seqids_(false),
	flags_(flags)
{
	if (flag_any(metadata, Metadata::TAXON_NODES | Metadata::TAXON_MAPPING | Metadata::TAXON_SCIENTIFIC_NAMES | Metadata::TAXON_RANKS))
		throw std::runtime_error("Taxonomy features are not supported for the BLAST database format.");
}

void BlastDB::init_seqinfo_access()
{
}

void BlastDB::init_seq_access()
{
}

void BlastDB::seek_chunk(const Chunk& chunk)
{
}

size_t BlastDB::tell_seq() const
{
	return oid_;
}

SeqInfo BlastDB::read_seqinfo()
{
	if (oid_ >= db_->GetNumOIDs()) {
		++oid_;
		return SeqInfo(0, 0);
	}
	const int l = db_->GetSeqLength(oid_);
	if (l == 0)
		throw std::runtime_error("Database with sequence length 0 is not supported");
	return SeqInfo(oid_++, l);
}

void BlastDB::putback_seqinfo()
{
	--oid_;
}

size_t BlastDB::id_len(const SeqInfo& seq_info, const SeqInfo& seq_info_next)
{
	if(flag_any(flags_, Flags::FULL_SEQIDS))
		return full_id(*db_->GetBioseq(seq_info.pos), nullptr, long_seqids_, true).length();
	else {
		return best_id(db_->GetSeqIDs(seq_info.pos)).length();
	}
}

void BlastDB::seek_offset(size_t p)
{
}

void BlastDB::read_seq_data(Letter* dst, size_t len, size_t& pos, bool seek)
{
	*(dst - 1) = Sequence::DELIMITER;
	*(dst + len) = Sequence::DELIMITER;
	const char* buf;
	const int db_len = db_->GetSequence((int)pos, &buf);
	std::copy(buf, buf + len, dst);
	++pos;
	db_->RetSequence(&buf);
}

void BlastDB::read_id_data(char* dst, size_t len)
{
	/*if (flag_any(flags_, Flags::FULL_SEQIDS)) {
		const string id = full_id(*db_->GetBioseq(oid_seqdata_), nullptr, long_seqids_, true);
		std::copy(id.begin(), id.begin() + len, dst);
	}
	else {
		const string id = best_id(db_->GetSeqIDs(oid_seqdata_));
		std::copy(id.begin(), id.end(), dst);
	}
	dst[len] = '\0';
	++oid_seqdata_;*/
}

void BlastDB::skip_id_data()
{
	//++oid_seqdata_;
}

std::string BlastDB::seqid(size_t oid)
{
	if (flag_any(flags_, Flags::FULL_SEQIDS)) {
		return full_id(*db_->GetBioseq(oid), nullptr, long_seqids_, true);
	}
	else {
		return acc_[oid];
	}
}

size_t BlastDB::sequence_count() const
{
	return db_->GetNumOIDs();
}

size_t BlastDB::sparse_sequence_count() const
{
	return db_->GetNumSeqs();	
}

size_t BlastDB::letters() const
{
	return db_->GetTotalLength();
}

int BlastDB::db_version() const
{
	return (int)db_->GetBlastDbVersion();
}

int BlastDB::program_build_version() const
{
	return 0;
}

void BlastDB::read_seq(std::vector<Letter>& seq, std::string& id)
{
	id.clear();
	CRef<CBioseq> bioseq = db_->GetBioseq(oid_);
	CScope scope(*CObjectManager::GetInstance());
	CBioseq_Handle bioseq_handle = scope.AddBioseq(*bioseq);

	id = full_id(*bioseq, &bioseq_handle, long_seqids_, false);

	seq.clear();
	load_seq_data(*bioseq, bioseq_handle, std::back_inserter(seq));
	
	++oid_;
}

SequenceFile::Metadata BlastDB::metadata() const
{
	return Metadata();
}

TaxonomyNodes* BlastDB::taxon_nodes()
{
	return nullptr;
}

std::vector<string>* BlastDB::taxon_scientific_names()
{
	return nullptr;
}

int BlastDB::build_version()
{
	return 0;
}

void BlastDB::create_partition_balanced(size_t max_letters)
{
}

void BlastDB::save_partition(const std::string& partition_file_name, const std::string& annotation)
{
}

size_t BlastDB::get_n_partition_chunks()
{
	return size_t();
}

void BlastDB::set_seqinfo_ptr(size_t i)
{
	oid_ = (int)i;
}

void BlastDB::close()
{
}

void BlastDB::close_weakly()
{
	db_.reset();
}

void BlastDB::reopen()
{
	if(db_.get() == nullptr)
		db_.reset(new CSeqDBExpert(file_name_, CSeqDB::eProtein));
}

BitVector BlastDB::filter_by_accession(const std::string& file_name)
{
	BitVector v(sequence_count());
	TextInputFile in(file_name);
	vector<string> accs;
	while (in.getline(), (!in.line.empty() || !in.eof())) {
		accs.push_back(in.line);
	}
	in.close();

	vector<CSeqDB::TOID> oids;
	try {
		db_->AccessionsToOids(accs, oids);
	}
	catch (CSeqDBException& e) {
		throw std::runtime_error(e.GetMsg());
	}

	for (size_t i = 0; i < accs.size(); ++i) {
		if (oids[i] < 0)
			if (config.skip_missing_seqids)
				message_stream << "WARNING: Accession not found in database : " + accs[i] << endl;
			else
				throw std::runtime_error("Accession not found in database: " + accs[i] + ". Use --skip-missing-seqids to ignore.");
		else
			v.set(oids[i]);
	}

	return v;
}

BitVector BlastDB::filter_by_taxonomy(const std::string& include, const std::string& exclude, TaxonomyNodes& nodes)
{
	return BitVector();
}

std::string BlastDB::file_name()
{
	return file_name_;
}

std::vector<unsigned> BlastDB::taxids(size_t oid) const
{
	return std::vector<unsigned>();
}

void BlastDB::seq_data(size_t oid, std::vector<Letter>& dst) const
{
	const char* buf;
	const int db_len = db_->GetSequence(oid, &buf);
	dst.clear();
	dst.resize(db_len);
	std::copy(buf, buf + db_len, dst.data());
	db_->RetSequence(&buf);
}

size_t BlastDB::seq_length(size_t oid) const
{
	return size_t();
}

void BlastDB::init_random_access()
{
	if (flag_any(flags_, Flags::FULL_SEQIDS))
		return;
	task_timer timer("Loading accessions");
	vector<string> paths;
	if(db_.get())
		db_->FindVolumePaths(paths);
	else
		CSeqDB::FindVolumePaths(file_name_, CSeqDB::eProtein, paths);
	acc_ = String_set<char, '\0'>();
	string acc;
	for (const string& path : paths) {
		TextInputFile f(path + ".acc");
		while (f.getline(), !f.eof() || !f.line.empty()) {
			Util::String::Tokenizer(f.line, "\t") >> acc;
			acc_.push_back(acc.begin(), acc.end());
		}
		f.close();
	}
}

void BlastDB::end_random_access()
{
	acc_ = String_set<char, '\0'>();
}

const BitVector* BlastDB::builtin_filter() {
	if (sequence_count() == sparse_sequence_count())
		return nullptr;
	if (oid_filter_.empty()) {
		oid_filter_ = BitVector(sequence_count());
		int oid = 0;
		while (db_->CheckOrFindOID(oid)) {
			oid_filter_.set(oid);
			++oid;
		}
	}
	return &oid_filter_;
}

BlastDB::~BlastDB()
{
}

void prep_blast_db() {
	vector<string> paths;
	CSeqDB::FindVolumePaths(config.database, CSeqDB::eProtein, paths);
	for (const string& db : paths) {
		message_stream << "Processing volume: " << db << endl;
		CSeqDB volume(db, CSeqDB::eProtein);
		const int n = volume.GetNumOIDs();
		message_stream << "Number of sequences: " << n << endl;
		ofstream out(db + ".acc");
		size_t id_count = 0;
		for (int i = 0; i < n; ++i) {
			list<CRef<CSeq_id>> ids = volume.GetSeqIDs(i);
			if (!ids.empty()) {
				auto it = ids.cbegin();
				out << (*it)->GetSeqIdString();
				while (++it != ids.cend())
					out << '\t' << (*it)->GetSeqIdString();
				id_count += ids.size();
			}
			out << endl;
		}
		message_stream << "Number of accessions: " << id_count << endl;
		out.close();
	}
}
