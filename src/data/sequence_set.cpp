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

#include "sequence_set.h"

SequenceSet::SequenceSet(Alphabet alphabet) :
	alphabet_(alphabet)
{ }

void SequenceSet::print_stats() const
{
	verbose_stream << "Sequences = " << this->get_length() << ", letters = " << this->letters() << ", average length = " << this->avg_len() << std::endl;
}

std::pair<size_t, size_t> SequenceSet::len_bounds(size_t min_len) const
{
	const size_t l(this->get_length());
	size_t max = 0, min = std::numeric_limits<size_t>::max();
	for (size_t i = 0; i < l; ++i) {
		max = std::max(this->length(i), max);
		min = this->length(i) >= min_len ? std::min(this->length(i), min) : min;
	}
	return std::pair<size_t, size_t>(min, max);
}

size_t SequenceSet::max_len(size_t begin, size_t end) const
{
	size_t max = 0;
	for (size_t i = begin; i < end; ++i)
		max = std::max(this->length(i), max);
	return max;
}

std::vector<size_t> SequenceSet::partition(unsigned n_part) const
{
	std::vector<size_t> v;
	const size_t l = (this->letters() + n_part - 1) / n_part;
	v.push_back(0);
	for (unsigned i = 0; i < this->get_length();) {
		size_t n = 0;
		while (i < this->get_length() && n < l)
			n += this->length(i++);
		v.push_back(i);
	}
	for (size_t i = v.size(); i < n_part + 1; ++i)
		v.push_back(this->get_length());
	return v;
}

size_t SequenceSet::reverse_translated_len(size_t i) const
{
	const size_t j(i - i % 6);
	const size_t l(this->length(j));
	if (this->length(j + 2) == l)
		return l * 3 + 2;
	else if (this->length(j + 1) == l)
		return l * 3 + 1;
	else
		return l * 3;
}

TranslatedSequence SequenceSet::translated_seq(const Sequence& source, size_t i) const
{
	if (!align_mode.query_translated)
		return TranslatedSequence((*this)[i]);
	return TranslatedSequence(source, (*this)[i], (*this)[i + 1], (*this)[i + 2], (*this)[i + 3], (*this)[i + 4], (*this)[i + 5]);
}

size_t SequenceSet::avg_len() const
{
	return this->letters() / this->get_length();
}

SequenceSet::~SequenceSet()
{ }

void SequenceSet::convert_to_std_alph(size_t id)
{
	if (alphabet_ == Alphabet::STD)
		return;
	Letter* ptr = this->ptr(id);
	const size_t len = length(id);
	for (size_t i = 0; i < len; ++i) {
		const char l = ptr[i];
		if ((size_t)l >= sizeof(NCBI_TO_STD) || NCBI_TO_STD[(int)l] < 0) {
			throw std::runtime_error("Unrecognized sequence character in BLAST database ("
				+ std::to_string((int)l)
				+ ", id=" + std::to_string(id)
				+ ", pos=" + std::to_string(i) + ')');
		}
		ptr[i] = NCBI_TO_STD[(int)l];
	}
}
