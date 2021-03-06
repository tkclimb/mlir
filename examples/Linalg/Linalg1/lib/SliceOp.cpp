//===- SliceOp.cpp - Implementation of the linalg SliceOp operation -------===//
//
// Copyright 2019 The MLIR Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// =============================================================================
//
// This file implements an IR operation to extract a "sub-View" from a ViewType
// in the Linalg dialect.
//
//===----------------------------------------------------------------------===//

#include "linalg1/Analysis.h"
#include "linalg1/Ops.h"
#include "linalg1/Types.h"
#include "linalg1/Utils.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/StandardTypes.h"

using namespace mlir;
using namespace linalg;

// A view may itself be coming either from a ViewOp or from a SliceOp.
// TODO assert statically or dynamically that indexing is within the bounds of
// view.
void linalg::SliceOp::build(Builder *b, OperationState *result, Value *view,
                            Value *indexing, unsigned dim) {
  // Early sanity checks + extract rank.
  unsigned rank = getViewRank(view);
  ViewType viewType = view->getType().cast<ViewType>();
  Type elementType = viewType.getElementType();

  result->addOperands({view, indexing});
  result->addAttribute(getSlicingDimAttrName(),
                       b->getIntegerAttr(b->getIndexType(), dim));
  if (indexing->getType().isa<RangeType>()) {
    // Taking a range slice does not decrease the rank, the view has the same
    // type.
    result->addTypes({viewType});
  } else {
    assert(indexing->getType().cast<IndexType>());
    result->addTypes(
        {linalg::ViewType::get(b->getContext(), elementType, rank - 1)});
  }
}

mlir::LogicalResult linalg::SliceOp::verify() {
  if (!getAttr(getSlicingDimAttrName()))
    return emitOpError("slice op expects a dim attribute");
  unsigned dim = getSlicingDim();
  if (dim >= getParentRank())
    return emitOpError("slicing dim must be in the [0 .. parent_rank) range");
  if (!getOperand(0)->getType().isa<ViewType>())
    return emitOpError(
        "first operand must be of ViewType (i.e. a ViewOp or a SliceOp)");
  auto type = getOperand(1)->getType().dyn_cast<IndexType>();
  auto range = getOperand(1)->getType().dyn_cast<RangeType>();
  if (!range && !type)
    return emitOpError(
        "second operand must be of RangeType (i.e. a RangeOp) or IndexType");
  return mlir::success();
}

ParseResult linalg::SliceOp::parse(OpAsmParser *parser,
                                   OperationState *result) {
  OpAsmParser::OperandType viewInfo;
  SmallVector<OpAsmParser::OperandType, 1> indexingInfo;
  SmallVector<Type, 8> types;
  if (parser->parseOperand(viewInfo) ||
      parser->parseOperandList(indexingInfo, 1,
                               OpAsmParser::Delimiter::Square) ||
      parser->parseOptionalAttributeDict(result->attributes) ||
      parser->parseColonTypeList(types))
    return failure();

  if (indexingInfo.size() != 1)
    return parser->emitError(parser->getNameLoc(), "expected 1 indexing type");

  ViewType viewType = types.front().dyn_cast<ViewType>();
  if (!viewType)
    return parser->emitError(parser->getNameLoc(),
                             "view type expected as first type");

  IndexType indexType = types.back().dyn_cast<IndexType>();
  RangeType rangeType = types.back().dyn_cast<RangeType>();
  if (!indexType && !rangeType) {
    llvm::errs() << types.back();
    return parser->emitError(parser->getNameLoc(),
                             "indexing must be of range or index type");
  }

  unsigned rank = viewType.getRank();
  if (indexType)
    --rank;
  ViewType resultViewType =
      ViewType::get(viewType.getContext(), viewType.getElementType(), rank);

  return failure(parser->resolveOperand(viewInfo, viewType, result->operands) ||
                 parser->resolveOperands(indexingInfo[0], types.back(),
                                         result->operands) ||
                 parser->addTypeToList(resultViewType, result->types));
}

// A SliceOp prints as:
//
// ```{.mlir}
//   linalg.slice %0[%i0] {dim = 0} : !linalg.view<?xf32>, index
// ```
//
// Where %0 is an ssa-value holding a `view<?x?xf32>`, %i0 is an ssa-value
// holding an index.
void linalg::SliceOp::print(OpAsmPrinter *p) {
  *p << getOperationName() << " " << *getParentView() << "[" << *getIndexing()
     << "]";
  *p << " {dim = " << getAttrOfType<IntegerAttr>("dim").getInt() << "}";
  p->printOptionalAttrDict(getAttrs(), {"dim"});
  *p << " : " << getParentViewType() << ", " << getIndexing()->getType();
}

ViewType linalg::SliceOp::getViewType() { return getType().cast<ViewType>(); }

unsigned linalg::SliceOp::getRank() { return getViewType().getRank(); }

mlir::Type linalg::SliceOp::getElementType() {
  return getViewType().getElementType();
}

ViewType linalg::SliceOp::getParentViewType() {
  return getParentView()->getType().cast<ViewType>();
}

unsigned linalg::SliceOp::getParentRank() {
  return getParentViewType().getRank();
}

mlir::Type linalg::SliceOp::getParentElementType() {
  return getParentViewType().getElementType();
}

bool linalg::SliceOp::isRankDecreasing() {
  return getParentRank() != getRank();
}

mlir::Operation::operand_range linalg::SliceOp::getIndexings() {
  return {this->getOperation()->operand_begin() + SliceOp::FirstIndexingOperand,
          this->getOperation()->operand_end()};
}
