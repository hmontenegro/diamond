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
#include <assert.h>
#include <vector>
#include <stddef.h>
#include "../basic/sequence.h"
#include "../util/algo/binary_search.h"

template<typename _t, char _pchar, size_t _padding = 1lu>
struct StringSetBase
{

	enum { PERIMETER_PADDING = 256 };
	static const char DELIMITER = _pchar;

	StringSetBase():
		data_ (PERIMETER_PADDING, _pchar)
	{
		limits_.push_back(PERIMETER_PADDING);
	}

	void finish_reserve()
	{
		data_.resize(raw_len() + PERIMETER_PADDING);
		std::fill(data_.begin() + raw_len(), data_.end(), _pchar);
	}

	void reserve(size_t n)
	{
		limits_.push_back(raw_len() + n + _padding);
	}

	void reserve(size_t entries, size_t length) {
		limits_.reserve(entries + 1);
		data_.reserve(length + 2 * PERIMETER_PADDING + entries * _padding);
	}

	void clear() {
		limits_.resize(1);
		data_.resize(PERIMETER_PADDING);
	}

	void shrink_to_fit() {
		limits_.shrink_to_fit();
		data_.shrink_to_fit();
	}

	template<typename _it>
	void push_back(_it begin, _it end)
	{
		assert(begin <= end);
		limits_.push_back(raw_len() + (end - begin) + _padding);
		data_.insert(data_.end(), begin, end);
		data_.insert(data_.end(), _padding, _pchar);
	}

	void fill(size_t n, _t v)
	{
		limits_.push_back(raw_len() + n + _padding);
		data_.insert(data_.end(), n, v);
		data_.insert(data_.end(), _padding, _pchar);
	}

	_t* ptr(size_t i)
	{ return &data_[limits_[i]]; }

	const _t* ptr(size_t i) const
	{ return &data_[limits_[i]]; }

	size_t check_idx(size_t i) const
	{
		if (limits_.size() < i + 2)
			throw std::runtime_error("Sequence set index out of bounds.");
		return i;
	}

	size_t length(size_t i) const
	{ return limits_[i+1] - limits_[i] - _padding; }

	size_t size() const
	{ return limits_.size() - 1; }

	bool empty() const {
		return limits_.size() <= 1;
	}

	size_t raw_len() const
	{ return limits_.back(); }

	size_t letters() const
	{ return raw_len() - size() - PERIMETER_PADDING; }

	_t* data(uint64_t p = 0)
	{ return &data_[p]; }

	const _t* data(uint64_t p = 0) const
	{ return &data_[p]; }

	size_t position(const _t* p) const
	{ return p - data(); }

	size_t position(size_t i, size_t j) const
	{ return limits_[i] + j; }

	std::pair<size_t, size_t> local_position(size_t p) const
	{
		size_t i = std::upper_bound(limits_.begin(), limits_.end(), p) - limits_.begin() - 1;
		return std::pair<size_t, size_t>(i, p - limits_[i]);
	}

	template<typename It, typename Out, typename Cmp>
	void local_position_batch(It begin, It end, Out out, Cmp cmp) const {
		batch_binary_search(begin, end, limits_.begin(), limits_.end(), out, cmp);
	}

	const _t* operator[](size_t i) const
	{
		return ptr(i);
	}

	typename std::vector<size_t>::const_iterator limits_begin() const {
		return limits_.begin();
	}

	typename std::vector<size_t>::const_iterator limits_end() const {
		return limits_.end();
	}

private:

	std::vector<_t> data_;
	std::vector<size_t> limits_;

};

using StringSet = StringSetBase<char, '\0'>;