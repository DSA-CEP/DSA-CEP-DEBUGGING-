#include <iostream>
#include <fstream>
#include <new>   // new (nothrow): allocation returns nullptr instead of throwing
#include <conio.h>
#include <windows.h>
using namespace std;

/*
 ===================== HOW THIS PROGRAM IS BUILT =====================

 NO std::string, std::vector, std::list or std::stack is used.
 Every piece of text and every collection uses the custom classes below.

 CUSTOM ADTs (written by us):

   TextBuffer          a growing character array (our own "string")
   DynamicArray<T>     a growing array (our own "vector")
   Stack<T>            a stack built on DynamicArray (used for undo / redo)
   Node + Document     doubly linked list, one node per line of text

 PROGRAM CLASSES (same design as before):

   Document       all lines (doubly linked list) + the cursor + editing
   Action         one edit that can be undone (type, position, text)
   UndoRedo       two stacks of Actions; undo() and redo() use them
   FileManager    saves and opens files, checks file names (no screen output)
   Selection      where the selection starts and what it covers
   Search         the matches of the last search and the current match
   EditorDisplay  draws everything on the console (changes nothing)
   TextEditor     the boss: reads keys and uses all the classes above

 What happens when you press a key (example: typing the letter 'a'):

   1. TextEditor::run()               draws the screen, then waits for a key
   2. TextEditor::handleNormalKey()   sees 'a' is a normal character
   3. Document::insertCharacterAt()   puts 'a' into the line (TextBuffer::insertChar)
   4. UndoRedo::record()              stores an Action so Ctrl+Z can undo it
   5. Document::setCursor()           moves the cursor one step right
   6. run() loops and EditorDisplay::render() draws the new text

 MEMORY RULES (the whole project follows these 4 rules):
   1. Every class that calls new[] / new owns that memory.
   2. The destructor frees it (delete[] for arrays, delete for nodes).
   3. Classes that own memory have a COPY CONSTRUCTOR and operator= that make
      a full copy (TextBuffer, DynamicArray), or copying is forbidden
      (Document, TextEditor).
   4. new (nothrow) is used, so if memory runs out we get nullptr and the
      edit is skipped instead of the program crashing.

 Reading order: TextBuffer -> DynamicArray -> Stack -> helpers -> Node ->
 Document -> UndoRedo -> FileManager -> Selection -> Search ->
 EditorDisplay (console details) -> TextEditor -> main.
 =====================================================================
*/

// Action types
const int INSERT_CHAR = 1;
const int DELETE_CHAR = 2;
const int SPLIT_LINE = 3;
const int JOIN_LINES = 4;
const int REPLACE_TEXT = 5;
const int MERGE_TEXT = 6;
const int DELETE_RANGE = 7;

// ================= TEXT BUFFER (our own string) =================

// Stores text in a character array that grows when it is full.
//
//   data      -> the characters, always followed by a '\0' (when data != nullptr)
//   length    -> how many characters are used
//   capacity  -> how many characters fit before we must grow
//
// Example: "Hi" with capacity 16 looks like  [H][i][\0][ ][ ]...(16 slots + 1)
//
// Ownership: a TextBuffer owns its array. The destructor frees it.
// Copying makes a NEW array (deep copy), so two buffers never share memory.
class TextBuffer
{
private:
    char *data;
    int length;
    int capacity;

    // Makes sure there is room for 'totalWanted' characters.
    // If the array is too small, a bigger array is allocated (at least double),
    // the old characters are copied over and the old array is freed.
    // Returns false if memory could not be allocated (text stays unchanged).
    bool makeRoom(int totalWanted)
    {
        if (totalWanted <= capacity)
        {
            return true;
        }

        int newCapacity = capacity * 2; // doubling keeps appending fast
        if (newCapacity < totalWanted)
            newCapacity = totalWanted;
        if (newCapacity < 16)
            newCapacity = 16;

        // +1 slot for the final '\0'.
        char *newData = new (nothrow) char[newCapacity + 1];

        if (newData == nullptr)
        {
            return false; // old array is still valid and untouched
        }

        for (int i = 0; i < length; i++)
        {
            newData[i] = data[i];
        }

        newData[length] = '\0';

        delete[] data; // free the OLD array (deleting nullptr is allowed)
        data = newData;
        capacity = newCapacity;
        return true;
    }

    // Changes the length and keeps the '\0' at the end.
    // data is only nullptr while the buffer has never held any text (length 0).
    void setLength(int newLength)
    {
        length = newLength;

        if (data != nullptr)
        {
            data[length] = '\0';
        }
    }

public:
    TextBuffer()
    {
        data = nullptr; // no memory is allocated for empty text
        length = 0;
        capacity = 0;
    }

    // Lets us write TextBuffer name = "hello";
    TextBuffer(const char *text)
    {
        data = nullptr;
        length = 0;
        capacity = 0;
        append(text);
    }

    // 'count' copies of one character, e.g. TextBuffer(5, '=') is "=====".
    TextBuffer(int count, char ch)
    {
        data = nullptr;
        length = 0;
        capacity = 0;
        appendRepeated(ch, count);
    }

    // Copy constructor: makes a NEW array with the same characters.
    TextBuffer(const TextBuffer &other)
    {
        data = nullptr;
        length = 0;
        capacity = 0;
        *this = other;
    }

    // Assignment: copy the characters of 'other' into our own array.
    TextBuffer &operator=(const TextBuffer &other)
    {
        if (this == &other)
        {
            return *this; // a = a does nothing
        }

        if (makeRoom(other.length))
        {
            for (int i = 0; i < other.length; i++)
            {
                data[i] = other.data[i];
            }

            setLength(other.length);
        }

        return *this;
    }

    ~TextBuffer()
    {
        delete[] data; // give the array back
    }

    int getLength() const
    {
        return length;
    }

    bool isEmpty() const
    {
        return length == 0;
    }

    // Returns the character at 'index', or '\0' if the index is invalid.
    char charAt(int index) const
    {
        if (index < 0 || index >= length)
        {
            return '\0';
        }

        return data[index];
    }

    // Changes one character. Returns false if the index is invalid.
    bool setChar(int index, char ch)
    {
        if (index < 0 || index >= length)
        {
            return false;
        }

        data[index] = ch;
        return true;
    }

    // Text as a normal C string (for cout and Windows functions).
    // Do not keep this pointer after the buffer changes.
    const char *c_str() const
    {
        if (data == nullptr)
        {
            return "";
        }

        return data;
    }

    void clear()
    {
        setLength(0); // memory is kept, so typing again is fast
    }

    // Cuts the text so only the first 'newLength' characters remain.
    void truncate(int newLength)
    {
        if (newLength >= 0 && newLength < length)
        {
            setLength(newLength);
        }
    }

    // Adds one character at the end.
    bool append(char ch)
    {
        if (!makeRoom(length + 1))
        {
            return false;
        }

        data[length] = ch;
        setLength(length + 1);
        return true;
    }

    // Adds a C string at the end (do not pass this buffer's own c_str()).
    bool append(const char *text)
    {
        if (text == nullptr)
        {
            return true;
        }

        int count = 0;
        while (text[count] != '\0')
        {
            count++;
        }

        if (!makeRoom(length + count))
        {
            return false;
        }

        for (int i = 0; i < count; i++)
        {
            data[length + i] = text[i];
        }

        setLength(length + count);
        return true;
    }

    // Adds another TextBuffer at the end.
    bool append(const TextBuffer &other)
    {
        if (&other == this)
        {
            // Appending to itself: copy first, because growing moves our array.
            TextBuffer copy(other);
            return append(copy);
        }

        if (!makeRoom(length + other.length))
        {
            return false;
        }

        for (int i = 0; i < other.length; i++)
        {
            data[length + i] = other.data[i];
        }

        setLength(length + other.length);
        return true;
    }

    // Adds the same character 'count' times.
    bool appendRepeated(char ch, int count)
    {
        if (count <= 0)
        {
            return true;
        }

        if (!makeRoom(length + count))
        {
            return false;
        }

        for (int i = 0; i < count; i++)
        {
            data[length + i] = ch;
        }

        setLength(length + count);
        return true;
    }

    // Adds a whole number as digits, e.g. 205 adds "205".
    bool appendNumber(int number)
    {
        long long value = number; // long long so that the biggest negative int works

        if (value < 0)
        {
            if (!append('-'))
                return false;
            value = -value;
        }

        char digits[24];
        int count = 0;

        if (value == 0)
        {
            digits[count++] = '0';
        }

        while (value > 0)
        {
            digits[count++] = (char)('0' + (value % 10)); // last digit first
            value = value / 10;
        }

        while (count > 0) // so write them backwards
        {
            if (!append(digits[--count]))
                return false;
        }

        return true;
    }

    // Inserts one character so that it ends up at index 'position'.
    // All characters from 'position' on move one step to the right.
    // Valid positions: 0 (start) up to length (end).
    bool insertChar(int position, char ch)
    {
        if (position < 0 || position > length)
        {
            return false;
        }

        if (!makeRoom(length + 1))
        {
            return false;
        }

        // Move from the back so nothing is overwritten.
        for (int i = length; i > position; i--)
        {
            data[i] = data[i - 1];
        }

        data[position] = ch;
        setLength(length + 1);
        return true;
    }

    // Removes 'count' characters starting at 'position'.
    // The characters after them move to the left.
    void eraseRange(int position, int count)
    {
        if (position < 0 || position >= length || count <= 0)
        {
            return;
        }

        if (count > length - position)
        {
            count = length - position; // never go past the end
        }

        for (int i = position; i + count < length; i++)
        {
            data[i] = data[i + count];
        }

        setLength(length - count);
    }

    // Returns a NEW buffer with 'count' characters starting at 'from'.
    // Out-of-range values are cut to the valid range, so it never fails.
    TextBuffer substr(int from, int count) const
    {
        TextBuffer result;

        if (from < 0)
            from = 0;
        if (from > length)
            from = length;
        if (count > length - from)
            count = length - from;

        if (count > 0 && result.makeRoom(count))
        {
            for (int i = 0; i < count; i++)
            {
                result.data[i] = data[from + i];
            }

            result.setLength(count);
        }

        return result;
    }

    // Same characters in the same order?
    bool equals(const TextBuffer &other) const
    {
        if (length != other.length)
        {
            return false;
        }

        for (int i = 0; i < length; i++)
        {
            if (data[i] != other.data[i])
            {
                return false;
            }
        }

        return true;
    }

    // True if the text contains at least one character from 'characters'.
    bool containsAnyOf(const char *characters) const
    {
        for (int i = 0; i < length; i++)
        {
            for (int j = 0; characters[j] != '\0'; j++)
            {
                if (data[i] == characters[j])
                {
                    return true;
                }
            }
        }

        return false;
    }
};

// ================= DYNAMIC ARRAY (our own vector) =================

// A list of items stored in one array that doubles in size when it is full.
//
//   items     -> the array
//   count     -> how many items are used
//   capacity  -> how many items fit before growing
//
// T must have a default constructor and a working operator=.
// Ownership: the array belongs to this object. Copying makes a full copy.
template <class T>
class DynamicArray
{
private:
    T *items;
    int count;
    int capacity;
    T fallback; // returned by at() when the index is invalid (never crashes)

    // Doubles the capacity. Returns false if memory could not be allocated.
    bool grow()
    {
        int newCapacity = 8;

        if (capacity > 0)
        {
            newCapacity = capacity * 2;
        }

        T *newItems = new (nothrow) T[newCapacity];

        if (newItems == nullptr)
        {
            return false;
        }

        for (int i = 0; i < count; i++)
        {
            newItems[i] = items[i];
        }

        delete[] items;
        items = newItems;
        capacity = newCapacity;
        return true;
    }

public:
    DynamicArray()
    {
        items = nullptr;
        count = 0;
        capacity = 0;
    }

    // Copy constructor: new array, every item copied.
    DynamicArray(const DynamicArray &other)
    {
        items = nullptr;
        count = 0;
        capacity = 0;
        *this = other;
    }

    DynamicArray &operator=(const DynamicArray &other)
    {
        if (this == &other)
        {
            return *this;
        }

        // Allocate the new array first. If that fails, we keep our old data.
        int newCapacity = other.count;
        if (newCapacity < 1)
            newCapacity = 1;

        T *newItems = new (nothrow) T[newCapacity];

        if (newItems == nullptr)
        {
            return *this;
        }

        for (int i = 0; i < other.count; i++)
        {
            newItems[i] = other.items[i];
        }

        delete[] items;
        items = newItems;
        capacity = newCapacity;
        count = other.count;
        return *this;
    }

    ~DynamicArray()
    {
        delete[] items;
    }

    // Adds one item at the end. Returns false if memory ran out.
    bool add(const T &value)
    {
        if (count == capacity && !grow())
        {
            return false;
        }

        items[count] = value;
        count++;
        return true;
    }

    int size() const
    {
        return count;
    }

    bool isEmpty() const
    {
        return count == 0;
    }

    // Item at 'index'. For an invalid index a harmless dummy item is returned.
    T &at(int index)
    {
        if (index < 0 || index >= count)
        {
            return fallback;
        }

        return items[index];
    }

    const T &at(int index) const
    {
        if (index < 0 || index >= count)
        {
            return fallback;
        }

        return items[index];
    }

    // Forgets the last item (the slot is reused by the next add).
    void removeLast()
    {
        if (count > 0)
        {
            count--;
        }
    }

    // Forgets all items. The array memory is kept for reuse.
    void clear()
    {
        count = 0;
    }
};

// ================= STACK (built on DynamicArray) =================

// Last in, first out. push adds on top, pop removes the top item.
// The top of the stack is the LAST item of the array.
template <class T>
class Stack
{
private:
    DynamicArray<T> items;

public:
    bool push(const T &value)
    {
        return items.add(value);
    }

    // Copies the top item into 'value' and removes it. False if empty.
    bool pop(T &value)
    {
        if (items.isEmpty())
        {
            return false;
        }

        value = items.at(items.size() - 1);
        items.removeLast();
        return true;
    }

    bool isEmpty() const
    {
        return items.isEmpty();
    }

    void clear()
    {
        items.clear();
    }
};

// ================= SMALL TEXT HELPERS =================

// General-purpose text helpers: they belong to no single class.

// Lowercase version of one character, used for case-insensitive comparison.
char lowerChar(char ch)
{
    if (ch >= 'A' && ch <= 'Z')
    {
        return (char)(ch - 'A' + 'a'); // 'A'..'Z' are in a row, so add the distance
    }

    return ch;
}

// One search result, stored as zero-based line and column.
struct SearchMatch
{
    int line;
    int column;

    SearchMatch()
    {
        line = 0;
        column = 0;
    }
};

// ---- multi-line text: lines are stored as ONE text separated by '\n' ----

// Cuts the text into lines at every '\n' and puts them into 'lines'.
// Always gives at least one line.
void splitLines(const TextBuffer &text, DynamicArray<TextBuffer> &lines)
{
    lines.clear();
    TextBuffer currentLine;

    for (int i = 0; i < text.getLength(); i++)
    {
        char ch = text.charAt(i);

        if (ch == '\n')
        {
            lines.add(currentLine);
            currentLine.clear();
        }
        else
        {
            currentLine.append(ch);
        }
    }

    lines.add(currentLine);
}

// Opposite of splitLines: joins the lines with '\n' between them.
TextBuffer linesToText(const DynamicArray<TextBuffer> &lines)
{
    TextBuffer text;

    for (int i = 0; i < lines.size(); i++)
    {
        if (i > 0)
        {
            text.append('\n');
        }

        text.append(lines.at(i));
    }

    return text;
}

// Turns a typed "\n" (backslash + n) into a real line break, so the user can
// type multi-line search and replacement text on one console line.
TextBuffer unescapeLineBreaks(const TextBuffer &text)
{
    TextBuffer result;

    for (int i = 0; i < text.getLength(); i++)
    {
        if (text.charAt(i) == '\\' && i + 1 < text.getLength() && text.charAt(i + 1) == 'n')
        {
            result.append('\n');
            i++;
        }
        else
        {
            result.append(text.charAt(i));
        }
    }

    return result;
}

// Opposite of unescapeLineBreaks: shows a line break as "\n" so one-line
// messages (status line, search list) stay on one line.
TextBuffer displayText(const TextBuffer &text)
{
    TextBuffer result;

    for (int i = 0; i < text.getLength(); i++)
    {
        if (text.charAt(i) == '\n')
        {
            result.append("\\n");
        }
        else
        {
            result.append(text.charAt(i));
        }
    }

    return result;
}

// Reads one line typed by the user (without the '\n') into 'line'.
// Replaces getline(cin, string). Any line length is allowed.
// Returns false when the input has ended (nothing could be read).
bool readLine(TextBuffer &line)
{
    line.clear();
    char ch;
    bool readSomething = false;

    while (cin.get(ch))
    {
        readSomething = true;

        if (ch == '\n')
        {
            return true;
        }

        line.append(ch);
    }

    return readSomething;
}

// One replaced piece of text (may span several lines): 'before' and 'after'
// are the exact text at line/column before and after the replacement.
// Used by REPLACE_TEXT so one Action can undo/redo many replacements.
struct LineChange
{
    int line;
    int column;
    TextBuffer before;
    TextBuffer after;

    LineChange()
    {
        line = 0;
        column = 0;
    }
};

// Action class stores information about an editing operation.
// Copying an Action is safe: TextBuffer and DynamicArray copy themselves fully.
class Action
{
public:
    int type;
    int line;
    int column;
    TextBuffer text;

    int beforeLine;
    int beforeColumn;

    int afterLine;
    int afterColumn;

    // Only used by REPLACE_TEXT (one entry per replacement, last-in-document first).
    DynamicArray<LineChange> changes;

    Action()
    {
        type = 0;
        line = 0;
        column = 0;
        beforeLine = 0;
        beforeColumn = 0;
        afterLine = 0;
        afterColumn = 0;
    }

    Action(int t, int l, int c, const TextBuffer &txt,
           int bl, int bc, int al, int ac)
    {
        type = t;
        line = l;
        column = c;
        text = txt;
        beforeLine = bl;
        beforeColumn = bc;
        afterLine = al;
        afterColumn = ac;
    }
};

// Each node stores one line.
class Node
{
public:
    TextBuffer data; // the characters of this line (no '\n' inside)
    Node *next;
    Node *prev;

    Node(const TextBuffer &text)
    {
        data = text; // copies the characters
        next = nullptr;
        prev = nullptr;
    }
};

// Document management using a doubly linked list.
//
// Ownership: the Document owns EVERY node. Nodes are created with new
// (splitLineAt, replaceLines, constructor) and deleted with delete
// (joinLineAt, freeList). The destructor deletes all remaining nodes.
class Document
{
private:
    Node *head;
    Node *tail;
    int lineCount;
    Node *cursorLine;
    int cursorColumn;

    // Deletes a whole chain of nodes, starting at 'first'.
    void freeList(Node *first)
    {
        Node *current = first;

        while (current != nullptr)
        {
            Node *temp = current;
            current = current->next; // save next BEFORE deleting this node
            delete temp;
        }
    }

    // Used for a space inside the term (not the first character): if the
    // spaces/tabs and line breaks from line/column on contain at least one
    // line break, all of them are skipped and true is returned.
    // Otherwise nothing changes and false is returned.
    bool skipLineBreakGap(Node *&current, int &line, int &column)
    {
        Node *probe = current;
        int probeLine = line;
        int probeColumn = column;
        bool crossed = false;

        while (true)
        {
            if (probeColumn < probe->data.getLength())
            {
                char ch = probe->data.charAt(probeColumn);

                if (ch != ' ' && ch != '\t')
                {
                    break;
                }

                probeColumn++;
            }
            else if (probe->next != nullptr)
            {
                probe = probe->next;
                probeLine++;
                probeColumn = 0;
                crossed = true;
            }
            else
            {
                break;
            }
        }

        if (!crossed)
        {
            return false;
        }

        current = probe;
        line = probeLine;
        column = probeColumn;
        return true;
    }

    // Checks (ignoring case) whether the term is in the document at line/column,
    // where 'current' is the node of that line. The document is treated as ONE
    // continuous text: the end of every line (except the last) counts as a
    // single line-break character, so a match may run over several lines.
    //   - a space in the term matches a space OR a line break
    //     ("hello world" is also found when "hello" ends one line and
    //      "world" starts the next one, even if the lines have spaces or
    //      tabs at the end / start next to the line break)
    //   - a line break in the term matches only a line break
    // Gives the line and column where the match ends.
    bool matchesFrom(Node *current, int line, int column, const TextBuffer &term,
                     int &endLine, int &endColumn)
    {
        if (current == nullptr || column < 0 || column > current->data.getLength())
        {
            return false;
        }

        for (int i = 0; i < term.getLength(); i++)
        {
            char wanted = lowerChar(term.charAt(i));

            // A space (not at the start of the term) may stand for a line break.
            if (wanted == ' ' && i > 0 && skipLineBreakGap(current, line, column))
            {
                while (i + 1 < term.getLength() && term.charAt(i + 1) == ' ')
                {
                    i++;
                }

                continue;
            }

            if (column == current->data.getLength())
            {
                // End of the line: this position is a line break.
                if (current->next == nullptr || (wanted != '\n' && wanted != ' '))
                {
                    return false;
                }

                current = current->next;
                line++;
                column = 0;
            }
            else
            {
                if (lowerChar(current->data.charAt(column)) != wanted)
                {
                    return false;
                }

                column++;
            }
        }

        endLine = line;
        endColumn = column;
        return true;
    }

public:
    Document()
    {
        head = new Node(TextBuffer(""));
        tail = head;
        cursorLine = head;
        cursorColumn = 0;
        lineCount = 1;
    }

    // Copying is forbidden: the document owns its nodes (double delete).
    Document(const Document &) = delete;
    Document &operator=(const Document &) = delete;

    int getLineCount()
    {
        return lineCount;
    }

    // Counts all characters in the document, excluding newline characters.
    int getCharacterCount()
    {
        int total = 0;
        Node *current = head;

        while (current != nullptr)
        {
            total += current->data.getLength();
            current = current->next;
        }

        return total;
    }

    // Checks (ignoring case) whether the term is in the document at
    // line/column (see matchesFrom). Gives the line and column where the
    // match ends.
    bool matchesAt(int line, int column, const TextBuffer &term,
                   int &endLine, int &endColumn)
    {
        return matchesFrom(getLineAt(line), line, column, term, endLine, endColumn);
    }

    // Finds every match (case-insensitive) of the term in the whole document,
    // line breaks included, and puts them into 'matches'. Every position is
    // tried, the position of a line break too, so overlapping matches are
    // found as well. 'matches' is empty if the term is empty or nothing matches.
    void findAllMatches(const TextBuffer &searchTerm, DynamicArray<SearchMatch> &matches)
    {
        matches.clear();

        if (searchTerm.isEmpty())
        {
            return;
        }

        int lineNumber = 0;
        Node *current = head;

        while (current != nullptr)
        {
            for (int column = 0; column <= current->data.getLength(); column++)
            {
                int endLine, endColumn;

                if (matchesFrom(current, lineNumber, column, searchTerm,
                                endLine, endColumn))
                {
                    SearchMatch match;
                    match.line = lineNumber;
                    match.column = column;
                    matches.add(match);
                }
            }

            current = current->next;
            lineNumber++;
        }
    }

    // Returns the node of line number 'index', or nullptr if it does not exist.
    Node *getLineAt(int index)
    {
        if (index < 0 || index >= lineCount)
        {
            return nullptr;
        }

        Node *current;

        // Start from whichever end of the list is closer.
        if (index <= lineCount / 2)
        {
            current = head;

            for (int i = 0; i < index; i++)
            {
                current = current->next;
            }
        }
        else
        {
            current = tail;

            for (int i = lineCount - 1; i > index; i--)
            {
                current = current->prev;
            }
        }

        return current;
    }

    int getCursorColumn()
    {
        return cursorColumn;
    }

    int getCursorLineIndex()
    {
        int index = 0;
        Node *current = head;

        while (current != cursorLine && current != nullptr)
        {
            current = current->next;
            index++;
        }

        return index;
    }

    // Moves the cursor. An invalid line is ignored; the column is cut to
    // 0 .. length of that line.
    void setCursor(int line, int column)
    {
        Node *target = getLineAt(line);

        if (target == nullptr)
        {
            return;
        }

        cursorLine = target;

        if (column > target->data.getLength())
        {
            column = target->data.getLength();
        }

        if (column < 0)
        {
            column = 0;
        }

        cursorColumn = column;
    }

    // Returns true if the character was really inserted.
    bool insertCharacterAt(int line, int column, char ch)
    {
        Node *target = getLineAt(line);

        if (target == nullptr)
        {
            return false;
        }

        return target->data.insertChar(column, ch);
    }

    void deleteCharacterAt(int line, int column)
    {
        Node *target = getLineAt(line);

        if (target != nullptr)
        {
            target->data.eraseRange(column, 1);
        }
    }

    // Breaks one line into two at 'column'. The text after the column
    // moves into a new node that is linked in after the current line.
    // Returns false if nothing was changed (bad line or out of memory).
    bool splitLineAt(int line, int column)
    {
        Node *current = getLineAt(line);

        if (current == nullptr)
        {
            return false;
        }

        if (column < 0)
        {
            column = 0;
        }

        if (column > current->data.getLength())
        {
            column = current->data.getLength();
        }

        int expectedLength = current->data.getLength() - column;
        Node *newNode = new (nothrow) Node(current->data.substr(column, expectedLength));

        // If memory ran out, the node or its text is missing: change nothing.
        if (newNode == nullptr)
        {
            return false;
        }

        if (newNode->data.getLength() != expectedLength)
        {
            delete newNode;
            return false;
        }

        current->data.truncate(column); // the old line keeps only the left part

        // Link newNode between 'current' and current->next.
        newNode->next = current->next;
        newNode->prev = current;

        if (current->next != nullptr)
        {
            current->next->prev = newNode;
        }
        else
        {
            tail = newNode; // we added at the end of the list
        }

        current->next = newNode;
        lineCount++;
        return true;
    }

    // Adds the next line to the end of this line and removes the next node.
    // (The column parameter is not needed, it is kept so the calls stay the same.)
    // Returns false if nothing was changed.
    bool joinLineAt(int line, int column)
    {
        (void)column; // not used, see comment above
        Node *current = getLineAt(line);

        if (current == nullptr || current->next == nullptr)
        {
            return false;
        }

        Node *nextNode = current->next;

        // Copy the text first. If this fails, the next node is NOT deleted.
        if (!current->data.append(nextNode->data))
        {
            return false;
        }

        // Unlink nextNode from the list.
        current->next = nextNode->next;

        if (nextNode->next != nullptr)
        {
            nextNode->next->prev = current;
        }
        else
        {
            tail = current;
        }

        // The cursor must never point to a deleted node.
        if (cursorLine == nextNode)
        {
            cursorLine = current;
            cursorColumn = current->data.getLength();
        }

        delete nextNode; // we own the node, so we free it
        lineCount--;
        return true;
    }

    // Inserts multi-line text at line/column.
    // Example: inserting "AB\nCD" into "12|34" (cursor at |) gives
    //     12AB
    //     CD34
    // The text before the cursor joins the first inserted line and the
    // text after the cursor joins the last inserted line.
    // Also used to undo a selection deletion.
    void insertText(int line, int column, const TextBuffer &text)
    {
        DynamicArray<TextBuffer> lines;
        splitLines(text, lines);
        int count = lines.size();
        Node *first = getLineAt(line);

        if (first == nullptr)
        {
            return;
        }

        // Keep the column inside the line so substr() can never fail.
        int length = first->data.getLength();

        if (column < 0)
        {
            column = 0;
        }

        if (column > length)
        {
            column = length;
        }

        TextBuffer before = first->data.substr(0, column);                // text left of the cursor
        TextBuffer after = first->data.substr(column, length - column);   // text right of the cursor

        // One line only: just put the text in the middle.
        if (count == 1)
        {
            TextBuffer merged = before;
            bool ok = merged.append(lines.at(0)) && merged.append(after);

            if (ok)
            {
                first->data = merged;
            }

            return;
        }

        TextBuffer firstLine = before;

        if (!firstLine.append(lines.at(0)))
        {
            return;
        }

        first->data = firstLine;

        // Create one new line below the previous one for every extra inserted line.
        for (int i = 1; i < count; i++)
        {
            Node *previous = getLineAt(line + i - 1);

            if (!splitLineAt(line + i - 1, previous->data.getLength()))
            {
                return;
            }

            getLineAt(line + i)->data = lines.at(i);
        }

        // The old text that was after the cursor goes behind the last inserted line.
        getLineAt(line + count - 1)->data.append(after);
    }

    // Exact reverse of insertText. Also used to delete a selected range.
    // 'text' must be exactly the text that is in the document at line/column.
    void removeText(int line, int column, const TextBuffer &text)
    {
        DynamicArray<TextBuffer> lines;
        splitLines(text, lines);
        int count = lines.size();
        Node *first = getLineAt(line);
        Node *last = getLineAt(line + count - 1);

        if (first == nullptr || last == nullptr)
        {
            return;
        }

        // Column where the removed text ends, inside the last affected line.
        int endColumn = lines.at(count - 1).getLength();

        if (count == 1)
        {
            endColumn = column + lines.at(0).getLength();
        }

        // Keep both columns inside their lines so substr() can never fail.
        if (column < 0)
        {
            column = 0;
        }

        if (column > first->data.getLength())
        {
            column = first->data.getLength();
        }

        if (endColumn > last->data.getLength())
        {
            endColumn = last->data.getLength();
        }

        TextBuffer before = first->data.substr(0, column);                                // kept text before the range
        TextBuffer after = last->data.substr(endColumn, last->data.getLength() - endColumn); // kept text after the range

        // Join all affected lines into the first line, then replace its text
        // with just the kept parts.
        for (int i = 1; i < count; i++)
        {
            joinLineAt(line, column);
        }

        TextBuffer kept = before;

        if (kept.append(after))
        {
            first->data = kept;
        }
    }

    // Replaces every occurrence of 'term' (one or more lines) in the whole
    // document. Matches are taken left to right, never overlap, and the
    // replaced text is never searched again. Replacements are made from the
    // end of the document to the start so earlier positions stay correct.
    // Every real change is added to 'changes' in that order so the caller can
    // undo it. Returns how many replacements were made.
    int replaceAll(const TextBuffer &term, const TextBuffer &replacement,
                   DynamicArray<LineChange> &changes)
    {
        DynamicArray<SearchMatch> matches;
        DynamicArray<SearchMatch> chosen;
        findAllMatches(term, matches);
        int lastEndLine = 0;
        int lastEndColumn = 0;

        for (int i = 0; i < matches.size(); i++)
        {
            int endLine, endColumn;
            const SearchMatch &m = matches.at(i);

            // Skip a match that starts inside the previous replaced match.
            if (!chosen.isEmpty() && (m.line < lastEndLine ||
                                      (m.line == lastEndLine && m.column < lastEndColumn)))
            {
                continue;
            }

            matchesAt(m.line, m.column, term, endLine, endColumn);
            lastEndLine = endLine;
            lastEndColumn = endColumn;
            chosen.add(m);
        }

        for (int i = chosen.size() - 1; i >= 0; i--)
        {
            LineChange change;

            if (replaceAt(chosen.at(i).line, chosen.at(i).column, term, replacement, change))
            {
                changes.add(change);
            }
        }

        return chosen.size();
    }

    // Puts all lines of the document into 'lines'.
    void getLines(DynamicArray<TextBuffer> &lines)
    {
        lines.clear();
        Node *current = head;

        while (current != nullptr)
        {
            lines.add(current->data);
            current = current->next;
        }
    }

    // Replaces the match of 'term' (one or more lines) at line/column with
    // 'replacement'. 'change' receives the exact old and new text, so the
    // caller can undo it. Returns true if the text really changed.
    bool replaceAt(int line, int column, const TextBuffer &term,
                   const TextBuffer &replacement, LineChange &change)
    {
        int endLine, endColumn;

        if (!matchesAt(line, column, term, endLine, endColumn))
        {
            return false;
        }

        change.line = line;
        change.column = column;
        change.before = getText(line, column, endLine, endColumn);
        change.after = replacement;

        if (change.before.equals(change.after))
        {
            return false;
        }

        removeText(line, column, change.before);
        insertText(line, column, replacement);
        return true;
    }

    // Returns the text from line/column up to endLine/endColumn
    // (lines separated by '\n').
    TextBuffer getText(int line, int column, int endLine, int endColumn)
    {
        TextBuffer text;
        Node *current = getLineAt(line);

        for (int i = line; i <= endLine && current != nullptr; i++)
        {
            int from = (i == line) ? column : 0;
            int to = (i == endLine) ? endColumn : current->data.getLength();

            text.append(current->data.substr(from, to - from));

            if (i < endLine)
            {
                text.append('\n');
            }

            current = current->next;
        }

        return text;
    }

    // Replace document contents with lines loaded from a file.
    // The new list is built FIRST. Only if that works, the old list is freed.
    // So if memory runs out, the old document stays unchanged.
    bool replaceLines(const DynamicArray<TextBuffer> &lines)
    {
        Node *newHead = nullptr;
        Node *newTail = nullptr;
        int newCount = 0;

        if (lines.isEmpty())
        {
            newHead = new (nothrow) Node(TextBuffer(""));

            if (newHead == nullptr)
            {
                return false;
            }

            newTail = newHead;
            newCount = 1;
        }
        else
        {
            for (int i = 0; i < lines.size(); i++)
            {
                Node *newNode = new (nothrow) Node(lines.at(i));

                if (newNode == nullptr || newNode->data.getLength() != lines.at(i).getLength())
                {
                    delete newNode;      // deleting nullptr is allowed
                    freeList(newHead);   // throw away the half-built list
                    return false;
                }

                if (newHead == nullptr)
                {
                    newHead = newNode;
                    newTail = newNode;
                }
                else
                {
                    newTail->next = newNode;
                    newNode->prev = newTail;
                    newTail = newNode;
                }

                newCount++;
            }
        }

        freeList(head); // free every old node
        head = newHead;
        tail = newTail;
        lineCount = newCount;
        cursorLine = head;
        cursorColumn = 0;
        return true;
    }

    ~Document()
    {
        freeList(head);
    }
};

// Manages Undo and Redo.
class UndoRedo
{
private:
    Stack<Action> undoStack;
    Stack<Action> redoStack;

public:
    void record(const Action &action)
    {
        undoStack.push(action);
        redoStack.clear(); // a new edit makes the old redo history invalid
    }

    void clearHistory()
    {
        undoStack.clear();
        redoStack.clear();
    }

    void undo(Document &document)
    {
        Action action;

        if (!undoStack.pop(action))
        {
            return;
        }

        switch (action.type)
        {
        case INSERT_CHAR:
            document.deleteCharacterAt(
                action.line, action.column);
            break;

        case DELETE_CHAR:
            document.insertCharacterAt(
                action.line, action.column, action.text.charAt(0));
            break;

        case SPLIT_LINE:
            document.joinLineAt(
                action.line, action.column);
            break;

        case JOIN_LINES:
            document.splitLineAt(
                action.line, action.column);
            break;

        case REPLACE_TEXT:
            // Undo the replacements in the opposite order they were made.
            for (int i = action.changes.size() - 1; i >= 0; i--)
            {
                LineChange &c = action.changes.at(i);

                document.removeText(c.line, c.column, c.after);
                document.insertText(c.line, c.column, c.before);
            }
            break;

        case MERGE_TEXT:
            // Remove the whole merged block in one step.
            document.removeText(action.line, action.column, action.text);
            break;

        case DELETE_RANGE:
            // Put the deleted text back exactly where it was.
            document.insertText(action.line, action.column, action.text);
            break;
        }

        document.setCursor(
            action.beforeLine, action.beforeColumn);

        redoStack.push(action);
    }

    void redo(Document &document)
    {
        Action action;

        if (!redoStack.pop(action))
        {
            return;
        }

        switch (action.type)
        {
        case INSERT_CHAR:
            document.insertCharacterAt(
                action.line, action.column, action.text.charAt(0));
            break;

        case DELETE_CHAR:
            document.deleteCharacterAt(
                action.line, action.column);
            break;

        case SPLIT_LINE:
            document.splitLineAt(
                action.line, action.column);
            break;

        case JOIN_LINES:
            document.joinLineAt(
                action.line, action.column);
            break;

        case REPLACE_TEXT:
            // Make the same replacements again, in the same order.
            for (int i = 0; i < action.changes.size(); i++)
            {
                LineChange &c = action.changes.at(i);

                document.removeText(c.line, c.column, c.before);
                document.insertText(c.line, c.column, c.after);
            }
            break;

        case MERGE_TEXT:
            // Insert the whole merged block again in one step.
            document.insertText(action.line, action.column, action.text);
            break;

        case DELETE_RANGE:
            // Delete the same range again in one step.
            document.removeText(action.line, action.column, action.text);
            break;
        }

        document.setCursor(
            action.afterLine, action.afterColumn);

        undoStack.push(action);
    }
};

// ================= FILE MANAGER =================

// Only works with files and file names. It never prints, asks or waits for
// a key. Every result is returned to the caller, which decides what to show.
// Files are read and written one character at a time with ifstream/ofstream
// (no string class is used).
class FileManager
{
private:
    static TextBuffer folderName()
    {
        return TextBuffer("TextEditorFiles\\");
    }

public:
    // Creates the document folder if it does not already exist.
    static void createDocumentFolder()
    {
        CreateDirectoryA("TextEditorFiles", NULL);
    }

    // Returns true if a file or folder with this path already exists.
    static bool exists(const TextBuffer &path)
    {
        return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    // Only .txt is supported. Do not silently change another extension.
    static bool hasTxtExtension(const TextBuffer &filename)
    {
        int length = filename.getLength();

        return length > 4 &&
               filename.substr(length - 4, 4).equals(TextBuffer(".txt"));
    }

    // Rejects paths and Windows-invalid filename characters.
    static bool hasInvalidCharacters(const TextBuffer &filename)
    {
        return filename.containsAnyOf("\\/:*?\"<>|");
    }

    // "notes.txt" becomes "TextEditorFiles\\notes.txt".
    static TextBuffer buildPath(const TextBuffer &filename)
    {
        TextBuffer path = folderName();
        path.append(filename);
        return path;
    }

    // Save document as a normal readable text file.
    static bool saveFile(Document &document, const TextBuffer &filename)
    {
        ofstream file(filename.c_str());

        if (!file)
        {
            return false;
        }

        Node *current = document.getLineAt(0);

        while (current != nullptr)
        {
            file.write(current->data.c_str(), current->data.getLength());
            file.put('\n');
            current = current->next;
        }

        file.close();

        if (file.fail())
        {
            return false;
        }

        return true;
    }

    // Open a normal readable text file into the document.
    // On failure it returns false and puts the reason in 'error'.
    static bool openFile(Document &document, const TextBuffer &filename, TextBuffer &error)
    {
        ifstream file(filename.c_str());

        if (!file)
        {
            error = "ERROR: FILE NOT FOUND!";
            return false;
        }

        DynamicArray<TextBuffer> lines;
        TextBuffer line;
        bool lineStarted = false; // true when characters were read after the last '\n'
        char ch;

        while (file.get(ch))
        {
            if (ch == '\n')
            {
                if (!lines.add(line))
                {
                    error = "ERROR: OUT OF MEMORY!";
                    return false;
                }

                line.clear();
                lineStarted = false;
            }
            else
            {
                if (!line.append(ch))
                {
                    error = "ERROR: OUT OF MEMORY!";
                    return false;
                }

                lineStarted = true;
            }
        }

        if (file.bad())
        {
            error = "ERROR: COULD NOT READ FILE!";
            return false;
        }

        file.close();

        // The last line may have no '\n' after it.
        if (lineStarted)
        {
            lines.add(line);
        }

        if (!document.replaceLines(lines))
        {
            error = "ERROR: OUT OF MEMORY!";
            return false;
        }

        return true;
    }
};

// ================= SELECTION =================

// The selection runs from the anchor (where Shift was first pressed)
// to the current cursor position.
class Selection
{
private:
    bool active;
    int anchorLine;
    int anchorColumn;

public:
    Selection()
    {
        active = false;
        anchorLine = 0;
        anchorColumn = 0;
    }

    bool isActive() const
    {
        return active;
    }

    // Starts a selection at this position (the cursor position before the move).
    void start(int line, int column)
    {
        active = true;
        anchorLine = line;
        anchorColumn = column;
    }

    void clear()
    {
        active = false;
    }

    // Gets the selection as start/end positions (start is always before end).
    // Returns false when nothing is selected or the selection is empty.
    bool getRange(Document &document, int &startLine, int &startColumn,
                  int &endLine, int &endColumn) const
    {
        if (!active)
        {
            return false;
        }

        int cursorLine = document.getCursorLineIndex();
        int cursorColumn = document.getCursorColumn();

        // Is the anchor before the cursor (selecting forward)?
        bool anchorFirst = false;

        if (anchorLine < cursorLine)
        {
            anchorFirst = true;
        }
        else if (anchorLine == cursorLine && anchorColumn <= cursorColumn)
        {
            anchorFirst = true;
        }

        if (anchorFirst)
        {
            startLine = anchorLine;
            startColumn = anchorColumn;
            endLine = cursorLine;
            endColumn = cursorColumn;
        }
        else
        {
            startLine = cursorLine;
            startColumn = cursorColumn;
            endLine = anchorLine;
            endColumn = anchorColumn;
        }

        // Anchor and cursor on the same spot means an empty selection.
        if (startLine == endLine && startColumn == endColumn)
        {
            return false;
        }

        return true;
    }

    // Returns the selected text, with lines joined by '\n' (same format that
    // insertText and removeText use).
    TextBuffer getText(Document &document, int startLine, int startColumn,
                       int endLine, int endColumn) const
    {
        TextBuffer text;

        for (int i = startLine; i <= endLine; i++)
        {
            Node *node = document.getLineAt(i);

            if (node == nullptr)
            {
                break; // invalid line: stop instead of crashing
            }

            // The first line starts at the selection start, the last line ends
            // at the selection end. Lines in between are taken completely.
            int from = 0;
            if (i == startLine)
                from = startColumn;

            int to = node->data.getLength();
            if (i == endLine)
                to = endColumn;

            text.append(node->data.substr(from, to - from));

            if (i < endLine)
            {
                text.append('\n');
            }
        }

        return text;
    }
};

// ================= SEARCH =================

// Keeps the state of the current search: the search term, every match and
// which match is the current one. It does NOT move the cursor, read keys or
// print anything. The editor does that using the data stored here.
class Search
{
private:
    TextBuffer searchTerm;
    DynamicArray<SearchMatch> matches;
    int currentMatchIndex; // -1 means "no current match"

public:
    Search()
    {
        currentMatchIndex = -1;
    }

    // Stores the term, asks the document for all matches and selects the
    // first one. The term is stored even when nothing matches (F4 uses it).
    // An empty term is ignored: the old search stays as it was.
    bool find(Document &document, const TextBuffer &term)
    {
        if (term.isEmpty())
        {
            return false;
        }

        searchTerm = term;
        document.findAllMatches(term, matches);
        currentMatchIndex = -1;

        if (matches.isEmpty())
        {
            return false;
        }

        currentMatchIndex = 0;
        return true;
    }

    // Finds the matches of the stored term again (used by F4, so edits made
    // since the search are included). Keeps the current match number if it
    // still exists, otherwise goes back to the first match.
    bool refresh(Document &document)
    {
        if (searchTerm.isEmpty())
        {
            return false;
        }

        document.findAllMatches(searchTerm, matches);

        if (matches.isEmpty())
        {
            currentMatchIndex = -1;
            return false;
        }

        if (currentMatchIndex < 0 || currentMatchIndex >= matches.size())
        {
            currentMatchIndex = 0;
        }

        return true;
    }

    // Moves to the next match. After the last one it wraps to the first.
    // Returns false when there are no matches.
    bool nextMatch()
    {
        if (matches.isEmpty() || currentMatchIndex < 0)
        {
            return false;
        }

        currentMatchIndex++;

        if (currentMatchIndex >= matches.size())
        {
            currentMatchIndex = 0;
        }

        return true;
    }

    // Moves to the previous match. Before the first one it wraps to the last.
    // (No keyboard shortcut uses this yet.)
    bool previousMatch()
    {
        if (matches.isEmpty() || currentMatchIndex < 0)
        {
            return false;
        }

        currentMatchIndex--;

        if (currentMatchIndex < 0)
        {
            currentMatchIndex = matches.size() - 1;
        }

        return true;
    }

    // Selects one match by index. An invalid index changes nothing.
    bool setCurrentMatch(int index)
    {
        if (index < 0 || index >= matches.size())
        {
            return false;
        }

        currentMatchIndex = index;
        return true;
    }

    // Forgets everything: term and matches.
    void clear()
    {
        searchTerm.clear();
        matches.clear();
        currentMatchIndex = -1;
    }

    // Forgets only the matches and keeps the term. Called after the text
    // changes, because the old line/column numbers may no longer be correct.
    void clearResults()
    {
        matches.clear();
        currentMatchIndex = -1;
    }

    bool hasMatches() const
    {
        return !matches.isEmpty();
    }

    const DynamicArray<SearchMatch> &getMatches() const
    {
        return matches;
    }

    // Gives the line and column of the current match.
    // Returns false when there is no current match.
    bool getCurrentMatch(SearchMatch &result) const
    {
        if (currentMatchIndex < 0 || currentMatchIndex >= matches.size())
        {
            return false;
        }

        result = matches.at(currentMatchIndex);
        return true;
    }

    const TextBuffer &getSearchTerm() const
    {
        return searchTerm;
    }

    int getCurrentMatchIndex() const
    {
        return currentMatchIndex;
    }
};

// ================= EDITOR DISPLAY =================

// (CONSOLE INTERFACE DETAILS: Windows console drawing. Not part of the
//  data-structure assignment, you only need the big idea: it READS the
//  document and draws it.)
//
// Everything that is drawn on the console. It only reads the document,
// selection and search; it never changes them.
class EditorDisplay
{
private:
    // First document line shown in the window (vertical scrolling).
    int firstVisibleLine;

    // Writes one row of text to the console at position x, y.
    // The text is cut or padded with spaces to exactly 'width' characters,
    // so a line never wraps and old text on that row is always overwritten.
    void writeConsoleRow(HANDLE console, int x, int y, const TextBuffer &text, int width)
    {
        if (width <= 0)
            return;

        TextBuffer row = text;
        int length = row.getLength();

        if (length > width)
            row.truncate(width);
        else if (length < width)
            row.appendRepeated(' ', width - length);

        COORD position;
        position.X = (SHORT)x;
        position.Y = (SHORT)y;

        DWORD written = 0;
        WriteConsoleOutputCharacterA(console, row.c_str(), (DWORD)row.getLength(),
                                     position, &written);
    }

    // Sets the colors of one console row: normal everywhere, highlight colors
    // from startX to endX (relative to the row start, endX not included).
    void colorConsoleRow(HANDLE console, int x, int y, int width,
                         WORD normal, WORD highlight, int startX, int endX)
    {
        DWORD written = 0;

        COORD position;
        position.X = (SHORT)x;
        position.Y = (SHORT)y;
        FillConsoleOutputAttribute(console, normal, (DWORD)width, position, &written);

        if (startX < endX)
        {
            COORD selected;
            selected.X = (SHORT)(x + startX);
            selected.Y = (SHORT)y;
            FillConsoleOutputAttribute(console, highlight, (DWORD)(endX - startX),
                                       selected, &written);
        }
    }

    // Horizontal scrolling: how many characters of the cursor's line are hidden
    // on the left so that the cursor stays visible. 0 means no scrolling.
    int getHorizontalOffset(int cursorColumn, int textWidth, int lineLength)
    {
        int offset = 0;

        if (cursorColumn >= textWidth)
        {
            offset = cursorColumn - textWidth + 1;
        }

        if (offset > lineLength)
        {
            offset = lineLength;
        }

        return offset;
    }

    // Draws the 6 rows at the top: title, file name and cursor information.
    void drawHeader(HANDLE console, int left, int top, int width,
                    const TextBuffer &filename, Document &document, int cursorLine)
    {
        TextBuffer border(width, '=');
        TextBuffer heading("TEXT EDITOR");

        // Put the heading in the middle of the row.
        int headingStart = (width - heading.getLength()) / 2;
        if (headingStart < 0)
            headingStart = 0;

        TextBuffer headingRow(headingStart, ' ');
        headingRow.append(heading); // writeConsoleRow pads the rest with spaces

        TextBuffer shownName = filename;
        if (shownName.isEmpty())
            shownName = "Untitled";

        TextBuffer fileRow(" File: ");
        fileRow.append(shownName);

        TextBuffer infoRow(" Current Line: ");
        infoRow.appendNumber(cursorLine + 1);
        infoRow.append(" | Column: ");
        infoRow.appendNumber(document.getCursorColumn() + 1);
        infoRow.append(" | Total Lines: ");
        infoRow.appendNumber(document.getLineCount());
        infoRow.append(" | Characters: ");
        infoRow.appendNumber(document.getCharacterCount());

        writeConsoleRow(console, left, top, border, width);
        writeConsoleRow(console, left, top + 1, headingRow, width);
        writeConsoleRow(console, left, top + 2, border, width);
        writeConsoleRow(console, left, top + 3, fileRow, width);
        writeConsoleRow(console, left, top + 4, infoRow, width);
        writeConsoleRow(console, left, top + 5, TextBuffer(width, '-'), width);
    }

    // Draws the 5 rows at the bottom: shortcut help and the status message.
    void drawFooter(HANDLE console, int left, int footerTop, int width,
                    const TextBuffer &status)
    {
        TextBuffer lastRow(" Arrows Move | Enter New Line | Backspace/Delete Remove");

        if (!status.isEmpty())
        {
            lastRow = " ";
            lastRow.append(status);
        }

        writeConsoleRow(console, left, footerTop, TextBuffer(width, '-'), width);
        writeConsoleRow(console, left, footerTop + 1,
                        " Ctrl+S Save | Ctrl+A Save As | Ctrl+O Merge | Ctrl+F2 Search | F4 List Of Search Results", width);
        writeConsoleRow(console, left, footerTop + 2,
                        " Ctrl+Z Undo | Ctrl+Y Redo | F7 Replace All | F8 Replace One | Esc Menu", width);
        writeConsoleRow(console, left, footerTop + 3,
                        " Shift+Arrows Select | Del/Backspace Delete Selection", width);
        writeConsoleRow(console, left, footerTop + 4, lastRow, width);
    }

public:
    EditorDisplay()
    {
        firstVisibleLine = 0;
    }

    // Displays a message and waits for a key.
    static void showMessage(const TextBuffer &message)
    {
        system("cls");
        cout << message.c_str() << endl;
        cout << "\nPress any key to continue...";
        _getch();
    }

    // Lists every match of the current search with 1-based line and column
    // numbers (the editor header also counts lines and columns from 1).
    void showSearchResults(const Search &search)
    {
        CONSOLE_SCREEN_BUFFER_INFO info;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);

        // How many matches fit on one screen page.
        int height = info.srWindow.Bottom - info.srWindow.Top + 1;
        int pageSize = height - 8;
        if (pageSize < 1)
            pageSize = 1;

        const DynamicArray<SearchMatch> &matches = search.getMatches();
        int total = matches.size();

        system("cls");
        cout << "========== SEARCH RESULTS ==========" << endl;
        cout << "Search term: " << displayText(search.getSearchTerm()).c_str() << endl;
        cout << "Total matches: " << total << endl;
        cout << "------------------------------------" << endl;

        for (int i = 0; i < total; i++)
        {
            cout << "Match " << (i + 1) << ": Line " << (matches.at(i).line + 1)
                 << ", Column " << (matches.at(i).column + 1) << endl;

            // Pause when the screen is full so nothing scrolls out of view.
            if ((i + 1) % pageSize == 0 && i + 1 < total)
            {
                cout << "-- " << (i + 1) << " of " << total
                     << " shown. Press any key for more, Esc to stop --";

                int key = _getch();

                if (key == 0 || key == 224)
                    _getch(); // Skip the second code of an arrow/function key.

                cout << endl;

                if (key == 27)
                    break;
            }
        }

        cout << "\nPress any key to return to the editor...";

        int key = _getch();

        if (key == 0 || key == 224)
            _getch();
    }

    void render(Document &document, const Selection &selection, const Search &search,
                const TextBuffer &filename, const TextBuffer &status)
    {
        HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO info;
        GetConsoleScreenBufferInfo(console, &info);

        // Work inside the visible window, not the full scrollback buffer.
        int width = info.srWindow.Right - info.srWindow.Left + 1;
        int height = info.srWindow.Bottom - info.srWindow.Top + 1;
        int left = info.srWindow.Left;
        int top = info.srWindow.Top;

        if (width < 20 || height < 11)
            return;

        const int headerRows = 6;
        const int footerRows = 5;

        int totalLines = document.getLineCount();
        int cursorLine = document.getCursorLineIndex();

        // Every row looks like: " 12 | text". The number column is as wide as
        // the biggest line number, so the "|" signs line up.
        TextBuffer totalText;
        totalText.appendNumber(totalLines);
        int lineNumberWidth = totalText.getLength();
        int prefixWidth = 1 + lineNumberWidth + 3;

        // Number of screen columns left for the text itself.
        int textWidth = width - prefixWidth;
        if (textWidth < 1)
            textWidth = 1;

        // Vertical scrolling: firstVisibleLine is the document line shown in the
        // first content row. It only moves when the cursor leaves the window.
        int contentRows = height - headerRows - footerRows;
        if (contentRows < 1)
            contentRows = 1;

        if (cursorLine < firstVisibleLine)
            firstVisibleLine = cursorLine;
        if (cursorLine >= firstVisibleLine + contentRows)
            firstVisibleLine = cursorLine - contentRows + 1;
        if (firstVisibleLine > totalLines - contentRows)
        {
            firstVisibleLine = totalLines - contentRows;
            if (firstVisibleLine < 0)
                firstVisibleLine = 0;
        }

        // Only the cursor's line is scrolled horizontally (when it is very long).
        Node *cursorNode = document.getLineAt(cursorLine);

        if (cursorNode == nullptr)
            return;

        int cursorOffset = getHorizontalOffset(document.getCursorColumn(), textWidth,
                                               cursorNode->data.getLength());

        drawHeader(console, left, top, width, filename, document, cursorLine);

        // Selected text is shown with inverted colors.
        int selStartLine = 0, selStartColumn = 0, selEndLine = 0, selEndColumn = 0;
        bool hasSelection = selection.getRange(document, selStartLine, selStartColumn,
                                               selEndLine, selEndColumn);

        // The current search match is highlighted only while the cursor is still
        // on it, so the highlight disappears by itself once the cursor moves or
        // the text under it changes.
        // A multi-line match is highlighted on every line it covers.
        bool hasMatch = false;
        int matchEndLine = 0;
        int matchEndColumn = 0;
        const TextBuffer &searchTerm = search.getSearchTerm();
        SearchMatch match;

        if (!searchTerm.isEmpty() && search.getCurrentMatch(match))
        {
            if (match.line == cursorLine &&
                match.column == document.getCursorColumn() &&
                document.matchesAt(match.line, match.column, searchTerm,
                                   matchEndLine, matchEndColumn))
            {
                hasMatch = true;
            }
        }

        // Highlight colors = normal colors with foreground and background swapped.
        WORD normalAttribute = info.wAttributes;
        WORD highlightAttribute = ((normalAttribute & 0x0F) << 4) |
                                  ((normalAttribute & 0xF0) >> 4);

        // Draw the document rows.
        Node *current = document.getLineAt(firstVisibleLine);

        for (int row = 0; row < contentRows; row++)
        {
            int screenY = top + headerRows + row;
            int lineIndex = firstVisibleLine + row;

            // Past the last line: draw an empty row (this also erases old text).
            if (current == nullptr)
            {
                writeConsoleRow(console, left, screenY, TextBuffer(""), width);
                colorConsoleRow(console, left, screenY, width,
                                normalAttribute, highlightAttribute, 0, 0);
                continue;
            }

            TextBuffer number;
            number.appendNumber(lineIndex + 1);

            TextBuffer linePrefix(" ");
            linePrefix.appendRepeated(' ', lineNumberWidth - number.getLength());
            linePrefix.append(number);
            linePrefix.append(" | ");

            // lineOffset = number of hidden characters on the left of this row.
            TextBuffer visibleText = current->data;
            int lineOffset = 0;

            if (lineIndex == cursorLine)
            {
                lineOffset = cursorOffset;
                visibleText = visibleText.substr(lineOffset, visibleText.getLength());
            }

            TextBuffer fullRow = linePrefix;
            fullRow.append(visibleText);
            writeConsoleRow(console, left, screenY, fullRow, width);

            // Work out which screen columns of this row get the highlight color.
            // A selected line break is shown as one extra highlighted cell
            // after the text. highlightEnd is not included.
            int highlightStart = 0;
            int highlightEnd = 0;

            if (hasSelection && lineIndex >= selStartLine && lineIndex <= selEndLine)
            {
                int fromColumn = 0;
                if (lineIndex == selStartLine)
                    fromColumn = selStartColumn;

                int toColumn = current->data.getLength() + 1;
                if (lineIndex == selEndLine)
                    toColumn = selEndColumn;

                highlightStart = prefixWidth + fromColumn - lineOffset;
                if (highlightStart < prefixWidth)
                    highlightStart = prefixWidth;

                highlightEnd = prefixWidth + toColumn - lineOffset;
                if (highlightEnd > width)
                    highlightEnd = width;
            }

            // The search match is highlighted only when the selection did not
            // highlight anything on this row.
            if (hasMatch && highlightStart == highlightEnd &&
                lineIndex >= match.line && lineIndex <= matchEndLine)
            {
                int fromColumn = 0;
                if (lineIndex == match.line)
                    fromColumn = match.column;

                int toColumn = current->data.getLength() + 1; // line break cell
                if (lineIndex == matchEndLine)
                    toColumn = matchEndColumn;

                highlightStart = prefixWidth + fromColumn - lineOffset;
                if (highlightStart < prefixWidth)
                    highlightStart = prefixWidth;

                highlightEnd = prefixWidth + toColumn - lineOffset;
                if (highlightEnd > width)
                    highlightEnd = width;
                if (highlightEnd < highlightStart)
                    highlightEnd = highlightStart;
            }

            colorConsoleRow(console, left, screenY, width,
                            normalAttribute, highlightAttribute,
                            highlightStart, highlightEnd);

            current = current->next;
        }

        drawFooter(console, left, top + height - footerRows, width, status);

        // Place the blinking cursor and keep it inside the text area.
        int cursorX = left + prefixWidth + document.getCursorColumn() - cursorOffset;
        int cursorY = top + headerRows + (cursorLine - firstVisibleLine);
        int maxX = info.srWindow.Right;
        int maxY = top + headerRows + contentRows - 1;

        if (cursorX > maxX)
            cursorX = maxX;
        if (cursorX < left)
            cursorX = left;
        if (cursorY > maxY)
            cursorY = maxY;
        if (cursorY < top + headerRows)
            cursorY = top + headerRows;

        COORD cursorPosition;
        cursorPosition.X = (SHORT)cursorX;
        cursorPosition.Y = (SHORT)cursorY;
        SetConsoleCursorPosition(console, cursorPosition);
    }
};

// ================= TEXT EDITOR =================

// Coordinates everything: it reads the keyboard, asks the user for input,
// moves the cursor and edits the document, and uses the other classes:
//   Document    - the text and the cursor
//   UndoRedo    - the edit history
//   Search      - the search matches (it never moves the cursor itself)
//   Selection   - the selected range
//   EditorDisplay - drawing on the console
//   FileManager - saving and opening files
class TextEditor
{
private:
    Document document;
    UndoRedo history;
    Search search;
    Selection selection;
    EditorDisplay display;

    TextBuffer filename; // "" means a new document that was never saved
    TextBuffer status;   // message shown in the last row

    // ---------- Asking for file names and opening files ----------

    // Shows an error under the filename prompt and waits for a key.
    void showFilenameError(const char *message)
    {
        cout << message << "\n";
        cout << "Press any key to try again...";
        _getch();
        cout << "\n";
    }

    // Gets a filename and validates that the user explicitly enters .txt.
    // requireUnique is used for new documents and Save As.
    // Returns the full path, or "" when the user enters nothing.
    TextBuffer getFilename(bool requireUnique = false)
    {
        TextBuffer name;

        while (true)
        {
            // Use a clean prompt screen so the filename input never overlaps
            // the editor's footer or the current editing cursor.
            system("cls");
            cout << "Enter filename (example: notes.txt): ";
            readLine(name);

            if (name.isEmpty())
                return TextBuffer("");

            if (!FileManager::hasTxtExtension(name))
            {
                showFilenameError("ERROR: INVALID FILE EXTENSION. ONLY .txt FILES ARE ALLOWED.");
                continue;
            }

            if (FileManager::hasInvalidCharacters(name))
            {
                showFilenameError("ERROR: INVALID FILENAME. ENTER A NAME, NOT A PATH.");
                continue;
            }

            TextBuffer fullPath = FileManager::buildPath(name);

            if (requireUnique && FileManager::exists(fullPath))
            {
                showFilenameError("ERROR: FILE NAME ALREADY USED. PLEASE ENTER A UNIQUE NAME.");
                continue;
            }

            return fullPath;
        }
    }

    // Opens a file into 'target' and shows the result message to the user.
    bool loadFile(Document &target, const TextBuffer &path)
    {
        TextBuffer error;

        if (!FileManager::openFile(target, path, error))
        {
            display.showMessage(error);
            return false;
        }

        display.showMessage("DOCUMENT OPENED SUCCESSFULLY!");
        return true;
    }

    // ---------- Search (Ctrl+F2, F3, F4) ----------

    // Text for the status line, for example "MATCH 2 OF 5: hello (CTRL+N/F3 NEXT, F4 LIST)".
    TextBuffer matchInfoText()
    {
        TextBuffer info("MATCH ");
        info.appendNumber(search.getCurrentMatchIndex() + 1);
        info.append(" OF ");
        info.appendNumber(search.getMatches().size());
        info.append(": ");
        info.append(displayText(search.getSearchTerm()));
        info.append(" (CTRL+N/F3 NEXT, F4 LIST)");
        return info;
    }

    // Shared by Ctrl+F2 and F4: moves the cursor to the current match, shows the
    // list of all matches and then sets the status line.
    void showFoundMatches()
    {
        SearchMatch match;

        if (search.getCurrentMatch(match))
        {
            document.setCursor(match.line, match.column);
        }

        display.showSearchResults(search);

        status = "FOUND ";
        status.appendNumber(search.getMatches().size());
        status.append(" MATCHES - ");
        status.append(matchInfoText());
    }

    // Returns the selected text (lines separated by '\n'), or "" if nothing is selected.
    TextBuffer getSelectedText()
    {
        int startLine, startColumn, endLine, endColumn;

        if (!selection.getRange(document, startLine, startColumn, endLine, endColumn))
        {
            return TextBuffer("");
        }

        return selection.getText(document, startLine, startColumn, endLine, endColumn);
    }

    // Asks for a word or phrase and finds all matches (Ctrl+Space, F2 and Ctrl+F2).
    // Line breaks are found automatically. If text was selected, pressing Enter
    // alone searches for the selected text (it may cover several lines).
    void searchDocument(const TextBuffer &selectedText)
    {
        system("cls");
        cout << "========== SEARCH DOCUMENT ==========" << endl;

        if (!selectedText.isEmpty())
        {
            cout << "Selected text: " << displayText(selectedText).c_str() << endl;
            cout << "Press Enter to search for it, or type another word or phrase: ";
        }
        else
        {
            cout << "Enter a word or phrase: ";
        }

        TextBuffer searchTerm;
        readLine(searchTerm);

        if (searchTerm.isEmpty())
        {
            searchTerm = selectedText;
        }
        else
        {
            searchTerm = unescapeLineBreaks(searchTerm); // typed \n still works
        }

        if (searchTerm.isEmpty())
        {
            status = "SEARCH CANCELLED - EMPTY SEARCH TERM";
            return;
        }

        // find() selects the first match when there is one.
        if (!search.find(document, searchTerm))
        {
            status = "NO MATCHES FOUND: ";
            status.append(displayText(searchTerm));
            return;
        }

        showFoundMatches();
    }

    // F4: shows the match list of the last search again. The matches are found
    // again first, so edits made since the search are included.
    void listSearchMatches()
    {
        if (search.getSearchTerm().isEmpty())
        {
            status = "SEARCH FIRST USING CTRL+F2";
            return;
        }

        if (!search.refresh(document))
        {
            status = "NO MATCHES FOUND: ";
            status.append(displayText(search.getSearchTerm()));
            return;
        }

        showFoundMatches();
    }

    // Ctrl+N and F3: moves to the next match (after the last one it wraps to the first).
    void goToNextSearchMatch()
    {
        if (!search.nextMatch())
        {
            status = "SEARCH FIRST USING CTRL+F2";
            return;
        }

        SearchMatch match;
        search.getCurrentMatch(match);
        document.setCursor(match.line, match.column);

        status = matchInfoText();
    }

    // ---------- Replace One (F8) and Replace All (F7) ----------

    // Asks for the search term and the replacement term.
    // Returns false if the search term is empty (operation cancelled).
    // An empty replacement is allowed and simply deletes the matched text.
    bool getReplaceTerms(const char *title, TextBuffer &searchTerm, TextBuffer &replacement)
    {
        system("cls");
        cout << "========== " << title << " ==========" << endl;
        cout << "Enter a word or phrase to find (line breaks are matched automatically): ";
        readLine(searchTerm);
        searchTerm = unescapeLineBreaks(searchTerm);

        if (searchTerm.isEmpty())
        {
            return false;
        }

        cout << "Enter the replacement (leave empty to delete): ";
        readLine(replacement);
        replacement = unescapeLineBreaks(replacement);

        return true;
    }

    // Replace One (F8): the user picks one occurrence with Up/Down,
    // Enter replaces it, Esc cancels without changing anything.
    void replaceOneOccurrence()
    {
        TextBuffer searchTerm;
        TextBuffer replacement;

        if (!getReplaceTerms("REPLACE ONE OCCURRENCE", searchTerm, replacement))
        {
            status = "REPLACE CANCELLED - EMPTY SEARCH TERM";
            return;
        }

        DynamicArray<SearchMatch> matches;
        document.findAllMatches(searchTerm, matches);

        if (matches.isEmpty())
        {
            status = "NO MATCHES FOUND: ";
            status.append(displayText(searchTerm));
            return;
        }

        // Remember the cursor so Esc can put it back.
        int originalLine = document.getCursorLineIndex();
        int originalColumn = document.getCursorColumn();

        int total = matches.size();
        int selected = 0;

        // Only used to highlight the selected match (even over several lines).
        Search preview;
        preview.find(document, searchTerm);

        // Let the user choose a match. The editor is redrawn each time so the
        // cursor and highlight jump to the selected match.
        while (true)
        {
            document.setCursor(matches.at(selected).line, matches.at(selected).column);
            preview.setCurrentMatch(selected);

            TextBuffer pickStatus("FOUND ");
            pickStatus.appendNumber(total);
            pickStatus.append(" | MATCH ");
            pickStatus.appendNumber(selected + 1);
            pickStatus.append(" OF ");
            pickStatus.appendNumber(total);
            pickStatus.append(" | UP/DOWN: MOVE | ENTER: REPLACE | ESC: CANCEL");

            display.render(document, selection, preview, filename, pickStatus);

            int key = _getch();

            // ESC cancels without touching the document.
            if (key == 27)
            {
                document.setCursor(originalLine, originalColumn);
                status = "REPLACE CANCELLED - DOCUMENT NOT CHANGED";
                return;
            }

            // Enter confirms the selected match.
            if (key == 13)
            {
                break;
            }

            // Arrow keys come as two codes: 0 or 224, then the real code.
            if (key == 0 || key == 224)
            {
                key = _getch();

                if (key == 72) // Up (wraps from the first match to the last)
                {
                    selected--;
                    if (selected < 0)
                        selected = total - 1;
                }
                else if (key == 80) // Down (wraps from the last match to the first)
                {
                    selected++;
                    if (selected >= total)
                        selected = 0;
                }
            }
        }

        SearchMatch match = matches.at(selected);

        // The document replaces the text and remembers the line before/after,
        // so undo/redo are exact.
        LineChange change;
        bool changed = document.replaceAt(match.line, match.column,
                                          searchTerm, replacement, change);

        // The new text may have several lines: the cursor goes to its end.
        DynamicArray<TextBuffer> newLines;
        splitLines(replacement, newLines);
        int afterLine = match.line + newLines.size() - 1;
        int afterColumn = newLines.at(newLines.size() - 1).getLength();

        if (newLines.size() == 1)
        {
            afterColumn += match.column;
        }

        // Undo puts the cursor back on the match, redo puts it after the new text.
        Action action(
            REPLACE_TEXT, match.line, match.column, replacement,
            match.line, match.column, afterLine, afterColumn);

        action.changes.add(change);

        // Nothing to record if the replacement is the same as the old text.
        if (changed)
        {
            history.record(action);
        }

        document.setCursor(afterLine, afterColumn);

        search.clearResults();

        status = "REPLACED MATCH ";
        status.appendNumber(selected + 1);
        status.append(" OF ");
        status.appendNumber(total);
        status.append(" WITH: ");
        status.append(displayText(replacement));
    }

    // Replace All (F7): replaces every occurrence in one
    // undoable action.
    void replaceAllOccurrences()
    {
        TextBuffer searchTerm;
        TextBuffer replacement;

        if (!getReplaceTerms("REPLACE ALL OCCURRENCES", searchTerm, replacement))
        {
            status = "REPLACE ALL CANCELLED - EMPTY SEARCH TERM";
            return;
        }

        int originalLine = document.getCursorLineIndex();
        int originalColumn = document.getCursorColumn();

        // ONE action holds the changes of all lines, so a single Ctrl+Z undoes
        // everything and a single Ctrl+Y redoes everything.
        Action action(
            REPLACE_TEXT, originalLine, originalColumn, replacement,
            originalLine, originalColumn, originalLine, originalColumn);

        int replacementCount = document.replaceAll(searchTerm, replacement,
                                                   action.changes);

        if (replacementCount == 0)
        {
            status = "NO MATCHES FOUND: ";
            status.append(displayText(searchTerm));
            return;
        }

        if (!action.changes.isEmpty())
        {
            history.record(action);
        }

        // Line lengths may have changed, so setCursor keeps the column valid.
        document.setCursor(originalLine, originalColumn);

        search.clearResults();

        status = "REPLACED ";
        status.appendNumber(replacementCount);
        status.append(" OCCURRENCE(S) OF: ");
        status.append(displayText(searchTerm));
        status.append(" (CTRL+Z TO UNDO)");
    }

    // ---------- Merge another file (Ctrl+O) ----------

    // Merge Document (Ctrl+O): inserts another .txt file at the cursor.
    // The source file is only read, never changed, and the current document
    // keeps its filename. The merge is stored as ONE undo action.
    void mergeDocument()
    {
        TextBuffer sourceName = getFilename();

        if (sourceName.isEmpty())
        {
            status = "MERGE CANCELLED";
            return;
        }

        // Read the source into a temporary document so the current one is kept.
        Document source;

        if (!loadFile(source, sourceName))
        {
            status = "MERGE FAILED - NOTHING WAS CHANGED";
            return;
        }

        // Take the lines of the source document.
        DynamicArray<TextBuffer> lines;
        source.getLines(lines);

        // An empty file is loaded as one empty line, so there is nothing to add.
        if (lines.size() == 1 && lines.at(0).isEmpty())
        {
            status = "NOTHING TO MERGE - SOURCE FILE IS EMPTY";
            return;
        }

        int count = lines.size();
        TextBuffer text = linesToText(lines);

        int line = document.getCursorLineIndex();
        int column = document.getCursorColumn();

        // The cursor ends right after the last inserted character.
        int afterLine = line + count - 1;
        int afterColumn = lines.at(count - 1).getLength();

        if (count == 1)
        {
            afterColumn = column + lines.at(0).getLength();
        }

        Action action(
            MERGE_TEXT, line, column, text,
            line, column, afterLine, afterColumn);

        document.insertText(line, column, text);
        history.record(action);

        document.setCursor(afterLine, afterColumn);

        search.clearResults();

        status = "MERGED ";
        status.appendNumber(count);
        status.append(" LINE(S) FROM ");
        status.append(sourceName);
        status.append(" (CTRL+Z TO UNDO)");
    }

    // ---------- Delete the selected text ----------

    // Deletes the selected text (one word, a range, or several lines) as ONE
    // undoable action. Returns false if nothing is selected, so the caller
    // can fall back to the normal single-character delete.
    bool deleteSelection()
    {
        int startLine, startColumn, endLine, endColumn;

        if (!selection.getRange(document, startLine, startColumn, endLine, endColumn))
        {
            return false;
        }

        TextBuffer text = selection.getText(document, startLine, startColumn,
                                            endLine, endColumn);

        // Undo and redo both put the cursor at the start of the deleted range.
        Action action(
            DELETE_RANGE, startLine, startColumn, text,
            startLine, startColumn, startLine, startColumn);

        document.removeText(startLine, startColumn, text);
        history.record(action);

        document.setCursor(startLine, startColumn);
        selection.clear();

        search.clearResults();

        status = "SELECTION DELETED (CTRL+Z TO UNDO)";
        return true;
    }

    // ---------- Saving (Ctrl+S, Ctrl+A) ----------

    // Ctrl + S: saves to the current file. A new document asks for a name first.
    void saveDocument()
    {
        if (filename.isEmpty())
        {
            TextBuffer newFilename = getFilename(true);

            if (!newFilename.isEmpty())
                filename = newFilename;
        }

        if (filename.isEmpty())
            return;

        if (FileManager::saveFile(document, filename))
            status = "DOCUMENT SAVED SUCCESSFULLY";
        else
            status = "ERROR: DOCUMENT COULD NOT BE SAVED";
    }

    // Ctrl + A: Save As must always let the user choose a different name,
    // even if the document has already been saved before.
    void saveDocumentAs()
    {
        TextBuffer newFilename = getFilename(false);

        if (newFilename.isEmpty())
        {
            status = "SAVE AS CANCELLED";
            return;
        }

        // If this is a different existing file, ask before replacing it.
        if (FileManager::exists(newFilename) && !newFilename.equals(filename))
        {
            system("cls");
            cout << "The file already exists:\n"
                 << newFilename.c_str() << "\n\n";
            cout << "Replace this file? (Y/N): ";
            int answer = _getch();
            cout << (char)answer << "\n";

            if (answer != 'y' && answer != 'Y')
            {
                status = "SAVE AS CANCELLED - ORIGINAL FILE KEPT";
                return;
            }
        }

        if (FileManager::saveFile(document, newFilename))
        {
            filename = newFilename;
            status = "DOCUMENT SAVED AS ";
            status.append(newFilename);
        }
        else
        {
            status = "ERROR: DOCUMENT COULD NOT BE SAVED";
        }
    }

    // ---------- Moving the cursor and deleting characters ----------

    // Moves the cursor for the Up, Down, Left, Right, Home and End keys.
    // 'line' and 'column' are the cursor position before the move.
    void moveCursor(int key, int line, int column)
    {
        Node *current = document.getLineAt(line);

        if (current == nullptr)
        {
            return;
        }

        switch (key)
        {
        case 72: // Up
            if (line > 0)
            {
                document.setCursor(line - 1, column);
            }
            break;

        case 80: // Down
            if (current->next != nullptr)
            {
                document.setCursor(line + 1, column);
            }
            break;

        case 75: // Left
            if (column > 0)
            {
                document.setCursor(line, column - 1);
            }
            else if (line > 0)
            {
                // At the start of a line: go to the end of the line above.
                Node *previous = document.getLineAt(line - 1);

                document.setCursor(line - 1, previous->data.getLength());
            }
            break;

        case 77: // Right
            if (column < current->data.getLength())
            {
                document.setCursor(line, column + 1);
            }
            else if (current->next != nullptr)
            {
                // At the end of a line: go to the start of the next line.
                document.setCursor(line + 1, 0);
            }
            break;

        case 71: // Home
            document.setCursor(line, 0);
            break;

        case 79: // End
            document.setCursor(line, current->data.getLength());
            break;
        }
    }

    // Delete key: removes the character under the cursor, or joins the next line
    // to this one when the cursor is at the end of the line.
    void deleteKeyPressed(int line, int column)
    {
        Node *current = document.getLineAt(line);

        if (current == nullptr)
        {
            return;
        }

        if (column < current->data.getLength())
        {
            TextBuffer removed(1, current->data.charAt(column));

            Action action(
                DELETE_CHAR, line, column, removed,
                line, column, line, column);

            history.record(action);

            document.deleteCharacterAt(line, column);
        }
        else if (current->next != nullptr)
        {
            Action action(
                JOIN_LINES, line, column, "",
                line, column, line, column);

            // Record only if the join really happened.
            if (document.joinLineAt(line, column))
            {
                history.record(action);
            }
        }
    }

    // Backspace key: removes the character before the cursor, or joins this
    // line to the end of the line above when the cursor is at the line start.
    void backspacePressed(int line, int column)
    {
        if (column > 0)
        {
            Node *current = document.getLineAt(line);

            if (current == nullptr)
            {
                return;
            }

            TextBuffer removed(1, current->data.charAt(column - 1));

            Action action(
                DELETE_CHAR, line, column - 1, removed,
                line, column, line, column - 1);

            history.record(action);

            document.deleteCharacterAt(line, column - 1);
            document.setCursor(line, column - 1);
        }
        else if (line > 0)
        {
            Node *previous = document.getLineAt(line - 1);

            int previousLength = previous->data.getLength();

            Action action(
                JOIN_LINES, line - 1, previousLength, "",
                line, column, line - 1, previousLength);

            if (document.joinLineAt(line - 1, previousLength))
            {
                history.record(action);
                document.setCursor(line - 1, previousLength);
            }
        }
    }

    // ---------- Reading keys (called by run) ----------

    // Arrow keys, Delete and F-keys send two codes: 0 or 224 first, then the
    // real key code. 'key' is already the second code.
    void handleExtendedKey(int key, int line, int column)
    {
        // The selection is cleared below, so keep its text for the search prompt.
        TextBuffer selectedText;

        if (key == 32 || key == 60 || key == 0x5F)
        {
            selectedText = getSelectedText();
        }

        // Shift + a movement key starts or extends the selection.
        // _getch() cannot report Shift, so Windows is asked for its state.
        bool isMoveKey = (key == 72 || key == 80 || key == 75 ||
                          key == 77 || key == 71 || key == 79);
        bool shiftHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

        if (isMoveKey && shiftHeld)
        {
            // The selection starts where the cursor was before the move.
            if (!selection.isActive())
            {
                selection.start(line, column);
            }
        }
        else if (key == 83 && deleteSelection())
        {
            return; // Delete removed the selected text.
        }
        else
        {
            selection.clear(); // Any other key cancels the selection.
        }

        // Search: Ctrl+Space (can arrive as NUL followed by ASCII space,
        // code 32), F2 (code 60) and Ctrl+F2 (code 0x5F on MSVC).
        if (key == 32 || key == 60 || key == 0x5F)
        {
            searchDocument(selectedText);
            return;
        }

        // F3 (scan code 61) moves to the next match.
        if (key == 61)
        {
            goToNextSearchMatch();
            return;
        }

        // F4 (scan code 62) lists every match of the last search.
        if (key == 62)
        {
            listSearchMatches();
            return;
        }

        // F7 (scan code 65) replaces every occurrence.
        if (key == 65)
        {
            replaceAllOccurrences();
            return;
        }

        // F8 (scan code 66) replaces one selected occurrence.
        if (key == 66)
        {
            replaceOneOccurrence();
            return;
        }

        if (key == 83) // Delete
        {
            deleteKeyPressed(line, column);
            search.clearResults();
        }
        else
        {
            moveCursor(key, line, column);
        }
    }

    // Normal keys: Ctrl shortcuts, Enter, Backspace and printable characters.
    void handleNormalKey(int key, int line, int column)
    {
        // Backspace deletes the selected text; any other key cancels it.
        if (key == 8 && deleteSelection())
        {
            return;
        }

        selection.clear();

        switch (key)
        {
        case 1: // Ctrl + A - Save As
            saveDocumentAs();
            break;

        case 14: // Ctrl + N - Next search match (reliable fallback for F3)
            goToNextSearchMatch();
            break;

        case 19: // Ctrl + S - Save
            saveDocument();
            break;

        case 15: // Ctrl + O - Merge another document at the cursor
            mergeDocument();
            break;

        case 26: // Ctrl + Z
            history.undo(document);
            search.clearResults();
            break;

        case 25: // Ctrl + Y
            history.redo(document);
            search.clearResults();
            break;

        case 13: // Enter - split the line at the cursor
        {
            Action action(
                SPLIT_LINE, line, column, "",
                line, column, line + 1, 0);

            // Record only if the line was really split.
            if (document.splitLineAt(line, column))
            {
                history.record(action);
                document.setCursor(line + 1, 0);
                search.clearResults();
            }

            break;
        }

        case 8: // Backspace
            backspacePressed(line, column);
            search.clearResults();
            break;

        default:
            // Normal printable characters.
            if (key >= 32 && key <= 126)
            {
                Action action(
                    INSERT_CHAR, line, column,
                    TextBuffer(1, (char)key),
                    line, column, line, column + 1);

                // Record only if the character was really inserted.
                if (document.insertCharacterAt(line, column, (char)key))
                {
                    history.record(action);
                    document.setCursor(line, column + 1);
                    search.clearResults();
                }
            }

            break;
        }
    }

public:
    TextEditor()
    {
        filename = "";
        status = "";
    }

    // The editor owns a document, so it must not be copied.
    TextEditor(const TextEditor &) = delete;
    TextEditor &operator=(const TextEditor &) = delete;

    // Menu option 2: asks for a file name and opens that file.
    // Returns true when the document was opened and the editor can start.
    bool openExistingDocument()
    {
        system("cls");

        TextBuffer path = getFilename();

        if (path.isEmpty())
        {
            return false;
        }

        if (!loadFile(document, path))
        {
            return false;
        }

        filename = path;
        history.clearHistory();
        search.clear();
        selection.clear();
        return true;
    }

    // Main loop: draw, read one key, handle it. Esc returns to the main menu.
    void run()
    {
        bool running = true;
        status = "";

        selection.clear();

        while (running)
        {
            // While text is selected, the status row shows the selection shortcuts.
            int selStartLine, selStartColumn, selEndLine, selEndColumn;

            if (selection.getRange(document, selStartLine, selStartColumn,
                                   selEndLine, selEndColumn))
            {
                display.render(document, selection, search, filename,
                               "SELECTED | DEL/BACKSPACE: DELETE | SHIFT+ARROWS: CHANGE | OTHER KEY: CANCEL");
            }
            else
            {
                display.render(document, selection, search, filename, status);
            }

            int key = _getch();

            // ESC returns to the main menu.
            if (key == 27)
            {
                running = false;
                break;
            }

            int line = document.getCursorLineIndex();
            int column = document.getCursorColumn();

            if (key == 0 || key == 224)
            {
                handleExtendedKey(_getch(), line, column);
            }
            else
            {
                handleNormalKey(key, line, column);
            }
        }
    }
};

// ================= MAIN MENU =================

int main()
{
    // Create the document folder when the program starts.
    FileManager::createDocumentFolder();

    bool applicationRunning = true;

    while (applicationRunning)
    {
        system("cls");

        cout << "========== TEXT EDITOR ==========" << endl;
        cout << "1. Create New Document" << endl;
        cout << "2. Open Existing Document" << endl;
        cout << "3. Exit" << endl;
        cout << "=================================" << endl;
        cout << "Enter your choice: ";

        TextBuffer choice;

        // Input ended (for example Ctrl+Z on the menu): leave the program.
        if (!readLine(choice))
        {
            break;
        }

        if (choice.equals("1"))
        {
            TextEditor editor;

            editor.run();
        }
        else if (choice.equals("2"))
        {
            TextEditor editor;

            if (editor.openExistingDocument())
            {
                editor.run();
            }
        }
        else if (choice.equals("3"))
        {
            applicationRunning = false;
        }
        else
        {
            EditorDisplay::showMessage("INVALID MENU CHOICE!");
        }
    }

    system("cls");
    cout << "Thank you for using the Text Editor!" << endl;

    return 0;
}
