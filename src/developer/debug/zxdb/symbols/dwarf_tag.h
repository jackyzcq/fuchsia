// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_TAG_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_TAG_H_

#include <string>

namespace zxdb {

enum class DwarfTag : int {
  // Not a DWARF tag, this is used to indicate "not present."
  kNone = 0x00,

  // Type modifier for arrays ("foo[]") of an underlying type. May have a SubrangeType child that
  // indicates the size of the array.
  kArrayType = 0x01,

  // C++ class definition.
  kClassType = 0x02,

  // "Alternate entry point" to a function. Seems to be not generated.
  kEntryPoint = 0x03,

  // C/C++ "enum" declaration. May have children of kEnumerator.
  kEnumerationType = 0x04,

  // Normal function parameter, seen as a child of a "subprogram." It will normally have at least a
  // name and a type.
  kFormalParameter = 0x05,

  // Generated for "using" statements that bring a type into a namespace. Converted into a
  // TypeModifier class.
  kImportedDeclaration = 0x08,

  // Label (as used for "goto"). Probably don't need to handle.
  kLabel = 0x0a,

  // A lexical block will typically have children of kVariable for everything declared in it. It
  // will also often have ranges associated with it.
  kLexicalBlock = 0x0b,

  // Class member data.
  kMember = 0x0d,

  // Type modifier that indicates a pointer to an underlying type.
  kPointerType = 0x0f,

  // Type modifier that indicates a reference to an underlying type.
  kReferenceType = 0x10,

  kCompileUnit = 0x11,

  // Not used in C/C++ (they don't have a true primitive string type).
  kStringType = 0x12,

  // C/C++ struct declaration.
  kStructureType = 0x13,

  // Type for a C/C++ pointer to member function. See kPtrToMemberType.
  kSubroutineType = 0x15,

  // Typedef that provides a different name for an underlying type. Converted into a TypeModifier
  // class.
  kTypedef = 0x16,

  kUnionType = 0x17,

  // Indicates a C/C++ parameter of "...".
  kUnspecifiedParameters = 0x18,

  // Member of a VariantPart, used by Rust for an enum value.
  kVariant = 0x19,

  // Common block and common inclusion are used by Fortran. Can ignore.
  kCommonBlock = 0x1a,
  kCommonInclusion = 0x1b,

  // A member of a class or struct that indicates a type it inherits from.
  kInheritance = 0x1c,

  // Child of a subroutine indicating a section of code that's from another
  // subroutine that's been inlined.
  kInlinedSubroutine = 0x1d,

  kModule = 0x1e,

  // C++ Foo::* type. See kSubroutineType.
  kPtrToMemberType = 0x1f,

  // Used by Pascal. Can ignore.
  kSetType = 0x20,

  // In C++ this can be generated as the child of an array entry with a "type" of
  // "__ARRAY_SIZE_TYPE__" and a "count" indicating the size of the array.
  kSubrangeType = 0x21,

  // Pascal and Modula-2 "with" statement. Can ignore.
  kWithStmt = 0x22,

  // C++ "public", "private", "protected". Seems to not be generated.
  kAccessDeclaration = 0x23,

  // Declaration of a built-in compiler base type like an "int".
  kBaseType = 0x24,

  kCatchBlock = 0x25,

  // Type modifier that adds "const".
  kConstType = 0x26,

  // Named constant.
  kConstant = 0x27,

  // Member of an enumeration. Will be a child of an EnumerationType entry.
  kEnumerator = 0x28,

  // Used by Pascal to represent its native file type.
  kFileType = 0x29,

  // C++ "friend" declaration. Seems to not be generated.
  kFriend = 0x2a,

  // Namelists are used in Fortran 90. Can ignore.
  kNamelist = 0x2b,
  kNamelistItem = 0x2c,

  // Packed types are used only by Pascal and ADA. Can ignore.
  kPackedType = 0x2d,

  // A function. Represented by a zxdb::Function object.
  kSubprogram = 0x2e,

  // Indicates the type (and possibly value) of a parameter in a template definition.
  kTemplateTypeParameter = 0x2f,
  kTemplateValueParameter = 0x30,

  kThrownType = 0x31,
  kTryBlock = 0x32,

  // Child of a structure that defines a Rust enum.
  kVariantPart = 0x33,

  // Local variable declaration. It will normally have a name, type, declaration location, and
  // location.
  kVariable = 0x34,

  // Type modifier that indicates adding "volatile" to an underlying type.
  kVolatileType = 0x35,
  kDwarfProcedure = 0x36,

  // Type modifier that indicates a C99 "restrict" qualifier on an underlying
  // type.
  kRestrictType = 0x37,

  // Java interface. Can ignore.
  kInterfaceType = 0x38,

  // C++ namespace. The declarations inside this will be the contents of the namespace. This will be
  // around declarations but not necessarily the function implementations.
  kNamespace = 0x39,

  // Seems to be generated for "using namespace" statements.
  kImportedModule = 0x3a,

  // Used in our toolchain for "decltype(nullptr)".
  kUnspecifiedType = 0x3b,

  kPartialUnit = 0x3c,
  kImportedUnit = 0x3d,

  // "If" statement. Seems to not be generated by our toolchain.
  kCondition = 0x3f,

  // Used by the "UPC" language. Can ignore.
  kSharedType = 0x40,

  // Seems to not be generated by our toolchain.
  kTypeUnit = 0x41,

  // Type modifier that indicates an rvalue reference to an underlying type.
  kRvalueReferenceType = 0x42,
  kTemplateAlias = 0x43,

  // For FORTRAN arrays.
  kCoarrayType = 0x44,
  kGenericSubrange = 0x45,

  // Not used in C/C++.
  kDynamicType = 0x46,

  // Atomic types like C11 _Atomic annotations.
  kAtomicType = 0x47,

  // Describes call site information. Clang doesn't currently generate these very often so they're
  // not very useful.
  kCallSite = 0x48,
  kCallSiteParameter = 0x49,

  // Used for split DWARF files.
  kSkeletonUnit = 0x4a,

  // Used in some languages like D, not in C/C++.
  kImmutableType = 0x4b,

  // -----------------------------------------------------------------------------------------------

  // Identifies one-past-the-end of the tags where we define constants. For range checking.
  kLastDefined,

  // User-defined range.
  kLoUser = 0x4080,
  kHiUser = 0xffff,
};

// Returns true if the tag defines a type.
bool DwarfTagIsType(DwarfTag tag);

// Returns true if the tag is one of the type modified variants (pointers, references, typedefs,
// const, volatile, etc.). See also "DwarfTagIsCVQualifier() variant below.
bool DwarfTagIsTypeModifier(DwarfTag tag);

// Returns true for C-V qualifiers. Also includes "restrict" for C99 and "_Atomic" for C11. This
// is basically anything that should be ignored to get an underlying type name, but does not invlude
// typdefs (since these have a different name).
bool DwarfTagIsCVQualifier(DwarfTag tag);

// Returns true if the dwarf tag is a reference or rvalue reference.
bool DwarfTagIsEitherReference(DwarfTag tag);

// Returns true if the tag is any reference type or a pointer.
bool DwarfTagIsPointerOrReference(DwarfTag tag);

// Converts the tag to a string, optionally including the numeric value. Invalid or unknown values
// will be formatted as "<undefined>" and will always include the numeric value.
std::string DwarfTagToString(DwarfTag tag, bool include_number = false);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_TAG_H_
