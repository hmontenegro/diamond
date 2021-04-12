/****
DIAMOND protein aligner
Copyright (C) 2020 Max Planck Society for the Advancement of Science e.V.

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

#include <limits.h>
#include "search.h"
#include "../data/queries.h"
#include "../data/reference.h"
#include "../dp/ungapped.h"
#include "../util/sequence/sequence.h"
#include "../dp/ungapped_simd.h"
#include "../dp/dp.h"
#include "left_most.h"
#include "../util/simd/vector.h"
#include "../dp/scan_diags.h"
#include "finger_print.h"
#include "../util/text_buffer.h"
#include "../util/memory/alignment.h"

using std::vector;

namespace Search {
namespace DISPATCH_ARCH {

static const int SHORT_QUERY_LEN = 85;

static int ungapped_cutoff(int query_len, const Context& context) {
#ifdef UNGAPPED_SPOUGE
	return query_len > config.short_query_max_len ? context.cutoff_table(query_len, 50) : context.short_query_ungapped_cutoff;
#else
	if (query_len <= config.short_query_max_len)
		return context.short_query_ungapped_cutoff;
	else if (query_len <= SHORT_QUERY_LEN && align_mode.query_translated)
		return context.cutoff_table_short(query_len);
	else
		return context.cutoff_table(query_len);
#endif
}

static int ungapped_window(int query_len) {
	if (query_len <= SHORT_QUERY_LEN && align_mode.query_translated)
		return query_len;
	else
		return config.ungapped_window;
}

static void search_query_offset(uint64_t q,
	const PackedLoc* s,
	FlatArray<uint32_t>::ConstIterator hits,
	FlatArray<uint32_t>::ConstIterator hits_end,
	WorkSet& work_set)
{
	thread_local TextBuffer output_buf;

	constexpr auto N = vector<Stage1_hit>::const_iterator::difference_type(::DISPATCH_ARCH::SIMD::Vector<int8_t>::CHANNELS);
	const bool long_subject_offsets = ::long_subject_offsets();
	const Letter* query = query_seqs::data_->data(q);

	const Letter* subjects[N];
	int scores[N];
	std::fill(scores, scores + N, INT_MAX);

	unsigned query_id = UINT_MAX, seed_offset = UINT_MAX;
	std::pair<size_t, size_t> l = query_seqs::data_->local_position(q);
	query_id = (unsigned)l.first;
	seed_offset = (unsigned)l.second;
	const int query_len = query_seqs::data_->length(query_id);
	const int score_cutoff = ungapped_cutoff(query_len, work_set.context);
	const int window = ungapped_window(query_len);
	const Sequence query_clipped = Util::Seq::clip(query - window, window * 2, window);
	const int window_left = int(query - query_clipped.data()), window_clipped = (int)query_clipped.length();
	const unsigned sid = work_set.shape_id;
	size_t hit_count = 0, n = 0;

	const int interval_mod = config.left_most_interval > 0 ? seed_offset % config.left_most_interval : window_left, interval_overhang = std::max(window_left - interval_mod, 0);

	for (FlatArray<uint32_t>::ConstIterator i = hits; i < hits_end; i += n) {

		n = std::min(N, hits_end - i);
		for (size_t j = 0; j < n; ++j)
			subjects[j] = ref_seqs::data_->data(s[*(i + j)]) - window_left;
		DP::window_ungapped_best(query_clipped.data(), subjects, n, window_clipped, scores);

		for (size_t j = 0; j < n; ++j) {
			if (scores[j] > score_cutoff) {
#ifdef UNGAPPED_SPOUGE
				std::pair<size_t, size_t> l = ref_seqs::data_->local_position(s[*(i + j)]);
				if (scores[j] < context.cutoff_table(query_len, ref_seqs::data_->length(l.first)))
					continue;
#endif
				work_set.stats.inc(Statistics::TENTATIVE_MATCHES2);
				if (left_most_filter(query_clipped + interval_overhang, subjects[j] + interval_overhang, window_left - interval_overhang, shapes[sid].length_, work_set.context, sid == 0, sid, score_cutoff)) {
					work_set.stats.inc(Statistics::TENTATIVE_MATCHES3);
					if (hit_count == 0) {
						output_buf.clear();
						output_buf.write_varint(query_id);
						output_buf.write_varint(seed_offset);
					}
					if (long_subject_offsets)
						output_buf.write_raw((const char*)&s[*(i + j)], 5);
					else
						output_buf.write(s[*(i + j)].low);
					output_buf.write((uint16_t)scores[j]);
					++hit_count;
				}
			}
		}
	}

	if (hit_count > 0) {
		if (long_subject_offsets)
			output_buf.write(packed_uint40_t(0));
		else
			output_buf.write((uint32_t)0);
		work_set.out.push(query_id / align_mode.query_contexts, output_buf.get_begin(), output_buf.size(), hit_count);
	}
}

static void FLATTEN search_tile(
	const FlatArray<uint32_t> &hits,
	uint32_t query_begin,
	uint32_t subject_begin,
	const PackedLoc* q,
	const PackedLoc* s,
	WorkSet& work_set)
{
	work_set.stats.inc(Statistics::TENTATIVE_MATCHES1, hits.data_size());
	const uint32_t query_count = (uint32_t)hits.size();
	const PackedLoc* q_begin = q + query_begin, *s_begin = s + subject_begin;
	for (uint32_t i = 0; i < query_count; ++i) {
		FlatArray<uint32_t>::ConstIterator r1 = hits.begin(i), r2 = hits.end(i);
		if (r2 == r1)
			continue;
		search_query_offset(q_begin[i], s_begin, r1, r2, work_set);
	}
}

typedef vector<FingerPrint, Util::Memory::AlignmentAllocator<FingerPrint, 16>> Container;

static void all_vs_all(const FingerPrint* a, uint32_t na, const FingerPrint* b, uint32_t nb, FlatArray<uint32_t>& out) {
	for (uint32_t i = 0; i < na; ++i) {
		const FingerPrint e = a[i];
		out.next();
		for (uint32_t j = 0; j < nb; ++j)
			if (e == b[j])
				out.push_back(j);
	}
}

static void load_fps(const PackedLoc* p, size_t n, Container& v, const SequenceSet& seqs)
{
	v.clear();
	v.reserve(n);
	const PackedLoc* end = p + n;
	for (; p < end; ++p)
		v.emplace_back(seqs.data(*p));
}

void FLATTEN stage1(const PackedLoc* q, size_t nq, const PackedLoc* s, size_t ns, WorkSet& work_set)
{
#ifdef __APPLE__
	thread_local Container vq, vs;
#else
	Container& vq = work_set.vq, &vs = work_set.vs;
#endif
	work_set.stats.inc(Statistics::SEED_HITS, nq * ns);
	load_fps(q, nq, vq, *query_seqs::data_);
	load_fps(s, ns, vs, *ref_seqs::data_);

	const size_t tile_size = config.tile_size;
	for (size_t i = 0; i < vq.size(); i += tile_size) {
		for (size_t j = 0; j < vs.size(); j += tile_size) {
			work_set.hits.clear();
			all_vs_all(vq.data() + i, (uint32_t)std::min(tile_size, vq.size() - i), vs.data() + j, (uint32_t)std::min(tile_size, vs.size() - j), work_set.hits);
			search_tile(work_set.hits, i, j, q, s, work_set);
		}
	}

}

}}