// Copyright 2017-2020 The Verible Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "common/formatting/align.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <numeric>
#include <vector>

#include "absl/strings/str_join.h"
#include "common/formatting/format_token.h"
#include "common/formatting/token_partition_tree.h"
#include "common/formatting/unwrapped_line.h"
#include "common/strings/display_utils.h"
#include "common/strings/range.h"
#include "common/text/concrete_syntax_leaf.h"
#include "common/text/concrete_syntax_tree.h"
#include "common/text/token_info.h"
#include "common/text/tree_utils.h"
#include "common/util/algorithm.h"
#include "common/util/container_iterator_range.h"
#include "common/util/logging.h"

namespace verible {

// Detects when there is a vertical separation of more than one line between
// two token partitions.
class BlankLineSeparatorDetector {
 public:
  // 'bounds' range must not be empty.
  explicit BlankLineSeparatorDetector(const TokenPartitionRange& bounds)
      : previous_end_(
            bounds.front().Value().TokensRange().front().token->text.begin()) {}

  bool operator()(const TokenPartitionTree& node) {
    const auto range = node.Value().TokensRange();
    if (range.empty()) return false;
    const auto begin = range.front().token->text.begin();
    const auto end = range.back().token->text.end();
    const auto gap = make_string_view_range(previous_end_, begin);
    // A blank line between partitions contains 2+ newlines.
    const bool new_bound = std::count(gap.begin(), gap.end(), '\n') >= 2;
    previous_end_ = end;
    return new_bound;
  }

 private:
  // Keeps track of the end of the previous partition, which is the start
  // of each inter-partition gap (string_view).
  absl::string_view::const_iterator previous_end_;
};

// Subdivides the 'bounds' range into sub-ranges broken up by blank lines.
static void FindPartitionGroupBoundaries(
    const TokenPartitionRange& bounds,
    std::vector<TokenPartitionIterator>* subpartitions) {
  VLOG(2) << __FUNCTION__;
  subpartitions->clear();
  if (bounds.empty()) return;
  subpartitions->push_back(bounds.begin());
  // Bookkeeping for the end of the previous token range, used to evaluate
  // the inter-token-range text, looking for blank line.
  verible::find_all(bounds.begin(), bounds.end(),
                    std::back_inserter(*subpartitions),
                    BlankLineSeparatorDetector(bounds));
  subpartitions->push_back(bounds.end());
  VLOG(2) << "end of " << __FUNCTION__
          << ", boundaries: " << subpartitions->size();
}

static int GetPartitionNodeEnum(const TokenPartitionTree& partition) {
  const Symbol* origin = partition.Value().Origin();
  return SymbolCastToNode(*origin).Tag().tag;
}

static bool VerifyRowsOriginalNodeTypes(
    const std::vector<TokenPartitionIterator>& rows) {
  const auto first_node_type = GetPartitionNodeEnum(*rows.front());
  for (const auto& row : verible::make_range(rows.begin() + 1, rows.end())) {
    const auto node_type = GetPartitionNodeEnum(*row);
    if (node_type != first_node_type) {
      VLOG(2) << "Cannot format-align rows of different syntax tree node "
                 "types.  First: "
              << first_node_type << ", Other: " << node_type;
      return false;
    }
  }
  return true;
}

static int EffectiveCellWidth(const FormatTokenRange& tokens) {
  if (tokens.empty()) return 0;
  VLOG(2) << __FUNCTION__;
  // Sum token text lengths plus required pre-spacings (except first token).
  // Note: LeadingSpacesLength() honors where original spacing when preserved.
  return std::accumulate(tokens.begin(), tokens.end(),
                         -tokens.front().LeadingSpacesLength(),
                         [](int total_width, const PreFormatToken& ftoken) {
                           VLOG(2) << " +" << ftoken.before.spaces_required
                                   << " +" << ftoken.token->text.length();
                           // TODO(fangism): account for multi-line tokens like
                           // block comments.
                           return total_width + ftoken.LeadingSpacesLength() +
                                  ftoken.token->text.length();
                         });
}

static int EffectiveLeftBorderWidth(const MutableFormatTokenRange& tokens) {
  if (tokens.empty()) return 0;
  return tokens.front().before.spaces_required;
}

struct AlignmentCell {
  // Slice of format tokens in this cell (may be empty range).
  MutableFormatTokenRange tokens;
  // The width of this token excerpt that complies with minimum spacing.
  int compact_width = 0;
  // Width of the left-side spacing before this cell, which can be considered
  // as a space-only column, usually no more than 1 space wide.
  int left_border_width = 0;

  FormatTokenRange ConstTokensRange() const {
    return FormatTokenRange(tokens.begin(), tokens.end());
  }

  void UpdateWidths() {
    compact_width = EffectiveCellWidth(ConstTokensRange());
    left_border_width = EffectiveLeftBorderWidth(tokens);
  }
};

std::ostream& operator<<(std::ostream& stream, const AlignmentCell& cell) {
  if (!cell.tokens.empty()) {
    // See UnwrappedLine::AsCode for similar printing.
    stream << absl::StrJoin(cell.tokens, " ",
                            [](std::string* out, const PreFormatToken& token) {
                              absl::StrAppend(out, token.Text());
                            });
  }
  return stream;
}

// These properties are calculated/aggregated from alignment cells.
struct AlignedColumnConfiguration {
  int width = 0;
  int left_border = 0;

  int TotalWidth() const { return left_border + width; }

  void UpdateFromCell(const AlignmentCell& cell) {
    width = std::max(width, cell.compact_width);
    left_border = std::max(left_border, cell.left_border_width);
  }
};

typedef std::vector<AlignmentCell> AlignmentRow;
typedef std::vector<AlignmentRow> AlignmentMatrix;

void ColumnSchemaScanner::ReserveNewColumn(
    const Symbol& symbol, const AlignmentColumnProperties& properties,
    const SyntaxTreePath& path) {
  // The path helps establish a total ordering among all desired alignment
  // points, given that they may come from optional or repeated language
  // constructs.
  const SyntaxTreeLeaf* leaf = GetLeftmostLeaf(symbol);
  // It is possible for a node to be empty, in which case, ignore.
  if (leaf == nullptr) return;
  if (sparse_columns_.empty() || sparse_columns_.back().path != path) {
    // It's possible the previous cell's path was intentionally altered
    // to effectively fuse it with the cell that is about to be added.
    // When this occurs, take the (previous) leftmost token, and suppress
    // adding a new column.
    sparse_columns_.push_back(
        ColumnPositionEntry{path, leaf->get(), properties});
    VLOG(2) << "reserving new column at " << TreePathFormatter(path);
  }
}

struct AggregateColumnData {
  // This is taken as the first seen set of properties in any given column.
  AlignmentColumnProperties properties;
  // These tokens's positions will be used to identify alignment cell
  // boundaries.
  std::vector<TokenInfo> starting_tokens;

  void Import(const ColumnPositionEntry& cell) {
    if (starting_tokens.empty()) {
      // Take the first set of properties, and ignore the rest.
      // They should be consistent, coming from alignment cell scanners,
      // but this is not verified.
      properties = cell.properties;
    }
    starting_tokens.push_back(cell.starting_token);
  }
};

class ColumnSchemaAggregator {
 public:
  void Collect(const std::vector<ColumnPositionEntry>& row) {
    for (const auto& cell : row) {
      cell_map_[cell.path].Import(cell);
    }
  }

  size_t NumUniqueColumns() const { return cell_map_.size(); }

  // Establishes 1:1 between SyntaxTreePath and column index.
  // Call this after collecting all columns.
  void FinalizeColumnIndices() {
    column_positions_.reserve(cell_map_.size());
    for (const auto& kv : cell_map_) {
      column_positions_.push_back(kv.first);
    }
  }

  const std::vector<SyntaxTreePath>& ColumnPositions() const {
    return column_positions_;
  }

  std::vector<AlignmentColumnProperties> ColumnProperties() const {
    std::vector<AlignmentColumnProperties> properties;
    properties.reserve(cell_map_.size());
    for (const auto& entry : cell_map_) {
      properties.push_back(entry.second.properties);
    }
    return properties;
  }

 private:
  // Keeps track of unique positions where new columns are desired.
  // The keys form the set of columns wanted across all rows.
  // The values are sets of starting tokens, from which token ranges
  // will be computed per cell.
  std::map<SyntaxTreePath, AggregateColumnData> cell_map_;

  // 1:1 map between SyntaxTreePath and column index.
  // Values are monotonically increasing, so this is binary_search-able.
  std::vector<SyntaxTreePath> column_positions_;
};

static SequenceStreamFormatter<AlignmentRow> MatrixRowFormatter(
    const AlignmentRow& row) {
  return SequenceFormatter(row, " | ", "< ", " >");
}

struct AlignmentRowData {
  // Range of format tokens whose space is to be adjusted for alignment.
  MutableFormatTokenRange ftoken_range;

  // Set of cells found that correspond to an ordered, sparse set of columns
  // to be aligned with other rows.
  std::vector<ColumnPositionEntry> sparse_columns;
};

// Translate a sparse set of columns into a fully-populated matrix row.
static void FillAlignmentRow(
    const AlignmentRowData& row_data,
    const std::vector<SyntaxTreePath>& column_positions, AlignmentRow* row) {
  VLOG(2) << __FUNCTION__;
  const auto& sparse_columns(row_data.sparse_columns);
  MutableFormatTokenRange partition_token_range(row_data.ftoken_range);
  // Translate token into preformat_token iterator,
  // full token range.
  const auto cbegin = column_positions.begin();
  const auto cend = column_positions.end();
  auto pos_iter = cbegin;
  auto token_iter = partition_token_range.begin();
  const auto token_end = partition_token_range.end();
  int last_column_index = 0;
  // Find each non-empty cell, and fill in other cells with empty ranges.
  for (const auto& col : sparse_columns) {
    pos_iter = std::find(pos_iter, cend, col.path);
    // By construction, sparse_columns' paths should be a subset of those
    // in the aggregated column_positions set.
    CHECK(pos_iter != cend);
    const int column_index = std::distance(cbegin, pos_iter);
    VLOG(3) << "cell at column " << column_index;

    // Find the format token iterator that corresponds to the column start.
    // Linear time total over all outer loop iterations.
    token_iter =
        std::find_if(token_iter, token_end, [=](const PreFormatToken& ftoken) {
          return BoundsEqual(ftoken.Text(), col.starting_token.text);
        });
    CHECK(token_iter != token_end);

    // Fill null-range cells between [last_column_index, column_index).
    const MutableFormatTokenRange empty_filler(token_iter, token_iter);
    for (; last_column_index <= column_index; ++last_column_index) {
      VLOG(3) << "empty at column " << last_column_index;
      (*row)[last_column_index].tokens = empty_filler;
    }
    // At this point, the current cell has only seen its lower bound.
    // The upper bound will be set in a separate pass.
  }
  // Fill any sparse cells up to the last column.
  VLOG(3) << "fill up to last column";
  const MutableFormatTokenRange empty_filler(token_end, token_end);
  for (const int n = column_positions.size(); last_column_index < n;
       ++last_column_index) {
    VLOG(3) << "empty at column " << last_column_index;
    (*row)[last_column_index].tokens = empty_filler;
  }

  // In this pass, set the upper bounds of cells' token ranges.
  auto upper_bound = token_end;
  for (auto& cell : verible::reversed_view(*row)) {
    cell.tokens.set_end(upper_bound);
    upper_bound = cell.tokens.begin();
  }
  VLOG(2) << "end of " << __FUNCTION__ << ", row: " << MatrixRowFormatter(*row);
}

struct MatrixCellSizeFormatter {
  const AlignmentMatrix& matrix;
};

std::ostream& operator<<(std::ostream& stream,
                         const MatrixCellSizeFormatter& p) {
  const AlignmentMatrix& matrix = p.matrix;
  for (auto& row : matrix) {
    stream << '['
           << absl::StrJoin(row, ", ",
                            [](std::string* out, const AlignmentCell& cell) {
                              absl::StrAppend(out, cell.left_border_width, "+",
                                              cell.compact_width);
                            })
           << ']' << std::endl;
  }
  return stream;
}

static void ComputeCellWidths(AlignmentMatrix* matrix) {
  VLOG(2) << __FUNCTION__;
  for (auto& row : *matrix) {
    for (auto& cell : row) {
      cell.UpdateWidths();
    }
  }
  VLOG(2) << "end of " << __FUNCTION__ << ", cell sizes:\n"
          << MatrixCellSizeFormatter{*matrix};
}

typedef std::vector<AlignedColumnConfiguration> AlignedFormattingColumnSchema;

static AlignedFormattingColumnSchema ComputeColumnWidths(
    const AlignmentMatrix& matrix) {
  VLOG(2) << __FUNCTION__;
  AlignedFormattingColumnSchema column_configs(matrix.front().size());
  for (auto& row : matrix) {
    auto column_iter = column_configs.begin();
    for (auto& cell : row) {
      column_iter->UpdateFromCell(cell);
      ++column_iter;
    }
  }
  VLOG(2) << "end of " << __FUNCTION__;
  return column_configs;
}

// Align cells by adjusting pre-token spacing for a single row.
static void AlignRowSpacings(
    const AlignedFormattingColumnSchema& column_configs,
    const std::vector<AlignmentColumnProperties>& properties,
    AlignmentRow* row) {
  VLOG(2) << __FUNCTION__;
  int accrued_spaces = 0;
  auto column_iter = column_configs.begin();
  auto properties_iter = properties.begin();
  for (const auto& cell : *row) {
    accrued_spaces += column_iter->left_border;
    if (cell.tokens.empty()) {
      // Accumulate spacing for the next sparse cell in this row.
      accrued_spaces += column_iter->width;
    } else {
      VLOG(2) << "at: " << cell.tokens.front().Text();
      // Align by setting the left-spacing based on sum of cell widths
      // before this one.
      const int padding = column_iter->width - cell.compact_width;
      int& left_spacing = cell.tokens.front().before.spaces_required;
      if (properties_iter->flush_left) {
        left_spacing = accrued_spaces;
        accrued_spaces = padding;
      } else {  // flush right
        left_spacing = accrued_spaces + padding;
        accrued_spaces = 0;
      }
      VLOG(2) << "left_spacing = " << left_spacing;
    }
    VLOG(2) << "accrued_spaces = " << accrued_spaces;
    ++column_iter;
    ++properties_iter;
  }
  VLOG(2) << "end of " << __FUNCTION__;
}

// Given a const_iterator and the original mutable container, return
// the corresponding mutable iterator (without resorting to const_cast).
// The 'Container' type is not deducible from function arguments alone.
// TODO(fangism): provide this from common/util/iterator_adaptors.
template <class Container>
typename Container::iterator ConvertToMutableIterator(
    typename Container::const_iterator const_iter,
    typename Container::iterator base) {
  const typename Container::const_iterator cbase(base);
  return base + std::distance(cbase, const_iter);
}

static MutableFormatTokenRange ConvertToMutableFormatTokenRange(
    const FormatTokenRange& const_range,
    MutableFormatTokenRange::iterator base) {
  using array_type = std::vector<PreFormatToken>;
  return MutableFormatTokenRange(
      ConvertToMutableIterator<array_type>(const_range.begin(), base),
      ConvertToMutableIterator<array_type>(const_range.end(), base));
}

static MutableFormatTokenRange GetMutableFormatTokenRange(
    const UnwrappedLine& unwrapped_line,
    MutableFormatTokenRange::iterator ftoken_base) {
  // Each row should correspond to an individual list element
  const Symbol* origin = ABSL_DIE_IF_NULL(unwrapped_line.Origin());
  VLOG(2) << "row: " << StringSpanOfSymbol(*origin);

  // Partition may contain text that is outside of the span of the syntax
  // tree node that was visited, e.g. a trailing comma delimiter.
  // Exclude those tokens from alignment consideration (for now).
  const SyntaxTreeLeaf* last_token = GetRightmostLeaf(*origin);
  const auto range_begin = unwrapped_line.TokensRange().begin();
  auto range_end = unwrapped_line.TokensRange().end();
  // Backwards search is expected to check at most a few tokens.
  while (!BoundsEqual(std::prev(range_end)->Text(), last_token->get().text))
    --range_end;
  CHECK(range_begin <= range_end);

  // Scan each token-range for cell boundaries based on syntax,
  // and establish partial ordering based on syntax tree paths.
  return ConvertToMutableFormatTokenRange(
      FormatTokenRange(range_begin, range_end), ftoken_base);
}

static void AlignFilteredRows(
    const std::vector<TokenPartitionIterator>& rows,
    const AlignmentCellScannerFunction& cell_scanner_gen,
    MutableFormatTokenRange::iterator ftoken_base, int column_limit) {
  VLOG(1) << __FUNCTION__;
  // Alignment requires 2+ rows.
  if (rows.size() <= 1) return;
  // Make sure all rows' nodes have the same type.
  if (!VerifyRowsOriginalNodeTypes(rows)) return;

  VLOG(2) << "Walking syntax subtrees for each row";
  ColumnSchemaAggregator column_schema;
  std::vector<AlignmentRowData> alignment_row_data;
  alignment_row_data.reserve(rows.size());
  // Simultaneously step through each node's tree, adding a column to the
  // schema if *any* row wants it.  This captures optional and repeated
  // constructs.
  for (const auto& row : rows) {
    // Each row should correspond to an individual list element
    const UnwrappedLine& unwrapped_line = row->Value();

    const AlignmentRowData row_data{
        // Extract the range of format tokens whose spacings should be adjusted.
        GetMutableFormatTokenRange(unwrapped_line, ftoken_base),
        // Scan each token-range for cell boundaries based on syntax,
        // and establish partial ordering based on syntax tree paths.
        cell_scanner_gen(*row)};

    alignment_row_data.emplace_back(row_data);
    // Aggregate union of all column keys (syntax tree paths).
    column_schema.Collect(row_data.sparse_columns);
  }

  // Map SyntaxTreePaths to column indices.
  VLOG(2) << "Mapping column indices";
  column_schema.FinalizeColumnIndices();
  const auto& column_positions = column_schema.ColumnPositions();
  const size_t num_columns = column_schema.NumUniqueColumns();
  VLOG(2) << "unique columns: " << num_columns;

  // Populate a matrix of cells, where cells span token ranges.
  // Null cells (due to optional constructs) are represented by empty ranges,
  // effectively width 0.
  VLOG(2) << "Filling dense matrix from sparse representation";
  AlignmentMatrix matrix(rows.size());
  {
    auto row_data_iter = alignment_row_data.cbegin();
    for (auto& row : matrix) {
      row.resize(num_columns);
      FillAlignmentRow(*row_data_iter, column_positions, &row);
      ++row_data_iter;
    }
  }

  // Compute compact sizes per cell.
  ComputeCellWidths(&matrix);

  // Compute max widths per column.
  AlignedFormattingColumnSchema column_configs(ComputeColumnWidths(matrix));

  // Extract other non-computed column properties.
  const auto column_properties = column_schema.ColumnProperties();

  // Total width does not include initial left-indentation.
  // Assume indentation is the same for all partitions in each group.
  const int indentation = rows.front().base()->Value().IndentationSpaces();
  const int total_column_width =
      std::accumulate(column_configs.begin(), column_configs.end(), indentation,
                      [](int total_width, const AlignedColumnConfiguration& c) {
                        return total_width + c.TotalWidth();
                      });
  VLOG(2) << "Total (aligned) column width = " << total_column_width;
  // if the aligned columns would exceed the column limit, then refuse to align
  // for now.  However, this check alone does not include text that follows
  // the last aligned column, like trailing commans and EOL comments.
  if (total_column_width > column_limit) {
    VLOG(1) << "Total aligned column width " << total_column_width
            << " exceeds limit " << column_limit
            << ", so not aligning this group.";
    return;
  }
  {
    auto partition_iter = rows.begin();
    for (const auto& row : matrix) {
      if (!row.empty()) {
        // Identify the unaligned epilog text on each partition.
        auto partition_end =
            partition_iter->base()->Value().TokensRange().end();
        auto row_end = row.back().tokens.end();
        const FormatTokenRange epilog_range(row_end, partition_end);
        const int aligned_partition_width =
            total_column_width + EffectiveCellWidth(epilog_range);
        if (aligned_partition_width > column_limit) {
          VLOG(1) << "Total aligned partition width " << aligned_partition_width
                  << " exceeds limit " << column_limit
                  << ", so not aligning this group.";
          return;
        }
      }
      ++partition_iter;
    }
  }

  // TODO(fangism): check for trailing text like comments, and if aligning would
  // exceed the column limit, then for now, refuse to align.
  // TODO(fangism): implement overflow mitigation fallback strategies.

  // Adjust pre-token spacings of each row to align to the column configs.
  for (auto& row : matrix) {
    AlignRowSpacings(column_configs, column_properties, &row);
  }
  VLOG(1) << "end of " << __FUNCTION__;
}

static void AlignPartitionGroup(
    const TokenPartitionRange& group,
    const AlignmentCellScannerFunction& alignment_scanner,
    std::function<bool(const TokenPartitionTree& node)> ignore_pred,
    MutableFormatTokenRange::iterator ftoken_base, int column_limit) {
  VLOG(1) << __FUNCTION__ << ", group size: " << group.size();
  // This partition group may contain partitions that should not be
  // considered for column alignment purposes, so filter those out.
  std::vector<TokenPartitionIterator> qualified_partitions;
  qualified_partitions.reserve(group.size());
  // like std::copy_if, but we want the iterators, not their pointees.
  for (auto iter = group.begin(); iter != group.end(); ++iter) {
    // TODO(fangism): pass in filter predicate as a function
    if (!ignore_pred(*iter)) {
      VLOG(2) << "including partition: " << *iter;
      qualified_partitions.push_back(iter);
    } else {
      VLOG(2) << "excluding partition: " << *iter;
    }
  }
  // Align the qualified partitions (rows).
  AlignFilteredRows(qualified_partitions, alignment_scanner, ftoken_base,
                    column_limit);
  VLOG(1) << "end of " << __FUNCTION__;
}

// TODO(fangism): move this to common/formatting/token_partition_tree
static absl::string_view StringSpanOfPartitionRange(
    const TokenPartitionRange& range) {
  const auto front_range = range.front().Value().TokensRange();
  const auto back_range = range.back().Value().TokensRange();
  CHECK(!front_range.empty());
  CHECK(!back_range.empty());
  return make_string_view_range(front_range.front().Text().begin(),
                                back_range.back().Text().end());
}

static bool AnyPartitionSubRangeIsDisabled(
    TokenPartitionRange range, absl::string_view full_text,
    const ByteOffsetSet& disabled_byte_ranges) {
  const absl::string_view span = StringSpanOfPartitionRange(range);
  const std::pair<int, int> span_offsets = SubstringOffsets(span, full_text);
  ByteOffsetSet diff(disabled_byte_ranges);  // copy
  diff.Complement(span_offsets);             // enabled range(s)
  ByteOffsetSet span_set;
  span_set.Add(span_offsets);
  return diff != span_set;
}

void TabularAlignTokens(
    TokenPartitionTree* partition_ptr,
    const AlignmentCellScannerFunction& alignment_scanner,
    const std::function<bool(const TokenPartitionTree&)> ignore_pred,
    MutableFormatTokenRange::iterator ftoken_base, absl::string_view full_text,
    const ByteOffsetSet& disabled_byte_ranges, int column_limit) {
  VLOG(1) << __FUNCTION__;
  // Each subpartition is presumed to correspond to a list element or
  // possibly some other ignored element like comments.

  auto& partition = *partition_ptr;
  auto& subpartitions = partition.Children();
  // Identify groups of partitions to align, separated by blank lines.
  const TokenPartitionRange subpartitions_range(subpartitions.begin(),
                                                subpartitions.end());
  if (subpartitions_range.empty()) return;
  std::vector<TokenPartitionIterator> subpartitions_bounds;
  // TODO(fangism): pass in custom alignment group partitioning function.
  FindPartitionGroupBoundaries(subpartitions_range, &subpartitions_bounds);
  CHECK_GE(subpartitions_bounds.size(), 2);
  auto prev = subpartitions_bounds.begin();
  // similar pattern to std::adjacent_difference.
  for (auto next = std::next(prev); next != subpartitions_bounds.end();
       prev = next, ++next) {
    const TokenPartitionRange group_partition_range(*prev, *next);

    // If any sub-interval in this range is disabled, skip it.
    // TODO(fangism): instead of disabling the whole range, sub-partition
    // it one more level, and operate on those ranges, essentially treating
    // no-format ranges like alignment group boundaries.
    // Requires IntervalSet::Intersect operation.
    if (group_partition_range.empty() ||
        AnyPartitionSubRangeIsDisabled(group_partition_range, full_text,
                                       disabled_byte_ranges))
      continue;

    AlignPartitionGroup(group_partition_range, alignment_scanner, ignore_pred,
                        ftoken_base, column_limit);
    // TODO(fangism): rewrite using functional composition.
  }
  VLOG(1) << "end of " << __FUNCTION__;
}

}  // namespace verible
