pragma Singleton
import QtQuick

// Centralized spacing and gap rules between blocks in the editor document view.
// Gaps are applied at the delegate boundary (bottomGap), keeping internal text
// baselines, gutters, and drag handles completely unaffected.
QtObject {
  id: spacing

  // BlockType enum mapping (matching Templnote::BlockType in BlockTypes.h):
  // Paragraph = 0, Heading = 1, Bullet = 2, NumberedList = 3, Todo = 4,
  // Quote = 5, Code = 6, Divider = 7, Image = 8, Toggle = 9, Callout = 10, Table = 11

  function isListType(type) {
    return type === 2 /* Bullet */ || type === 3 /* NumberedList */ || type === 4 /* Todo */
  }

  function isHeading(type) {
    return type === 1
  }

  // Returns the spacing gap (in pixels) below the block at index 'index'
  // transitioning to the block of type 'nextType'.
  function bottomGap(currentType, nextType, isLastBlock) {
    if (isLastBlock)
      return 0

    // 1. Same-type list items (Bullet -> Bullet, Numbered -> Numbered, Todo -> Todo)
    // Keep consecutive list items compact.
    if (isListType(currentType) && currentType === nextType)
      return 2

    // 2. List item transitioning to another list item of a different type
    if (isListType(currentType) && isListType(nextType))
      return 4

    // 3. Heading -> Any content following it:
    // Keep headings slightly closer to the paragraph/list they introduce.
    if (isHeading(currentType))
      return 4

    // 4. Any block -> Heading:
    // Add extra breathing room before a new section heading starts.
    if (isHeading(nextType))
      return 18

    // 5. List -> Paragraph or Paragraph -> List:
    // Moderate separation between list blocks and standard body text.
    if (isListType(currentType) || isListType(nextType))
      return 6

    // 6. Default gap between standard paragraphs and other blocks.
    return 6
  }
}
