#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <cctype>
#include <conio.h>
#include <windows.h>
using namespace std;

/*
 ===================== HOW THIS PROGRAM IS BUILT =====================

 Each class does ONE job:

   Node           one line of text (linked to the line before and after)
   Document       all lines (doubly linked list) + the cursor + editing
   Stack<T>       a simple stack, used for undo and redo
   Action         one edit that can be undone (type, position, text)
   UndoRedo       two stacks of Actions; undo() and redo() use them
   FileManager    saves and opens files, checks file names (no screen output)
   Selection      where the selection starts and what it covers
   Search         the matches of the last search and the current match
   EditorDisplay  draws everything on the console (changes nothing)
   TextEditor     the boss: reads keys and uses all the classes above

 What happens when you press a key (example: typing the letter 'a'):

   1. TextEditor::run()          draws the screen, then waits for a key
   2. TextEditor::handleNormalKey()   sees 'a' is a normal character
   3. UndoRedo::record()         stores an Action so Ctrl+Z can undo it
   4. Document::insertCharacterAt()   puts 'a' into the line
   5. Document::setCursor()      moves the cursor one step right
   6. run() loops and EditorDisplay::render() draws the new text

 Search: TextEditor asks Search::find(), Search asks Document for matches,
 TextEditor moves the cursor to the current match, EditorDisplay highlights it.

 Reading order: text helpers -> Node -> Document -> UndoRedo -> FileManager
 -> Selection -> Search -> EditorDisplay -> TextEditor -> main.
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

// ================= SMALL TEXT HELPERS =================

// General-purpose text helpers: they work on plain strings and belong to no single class.

// Returns a lowercase copy of the text, used for case-insensitive comparison.
string toLowerCopy(string text)
{
    for (int i = 0; i < text.length(); i++)
    {
        text[i] = tolower((unsigned char)text[i]);
    }

    return text;
}

// One search result, stored as zero-based line and column.
struct SearchMatch
{
    int line;
    int column;
};

// ---- multi-line text: lines are stored as ONE string separated by '\n' ----

// Cuts the text into lines at every '\n'. Always returns at least one line.
vector<string> splitLines(const string &text)
{
    vector<string> lines;
    string currentLine = "";

    for (int i = 0; i < text.length(); i++)
    {
        if (text[i] == '\n')
        {
            lines.push_back(currentLine);
            currentLine = "";
        }
        else
        {
            currentLine += text[i];
        }
    }

    lines.push_back(currentLine);
    return lines;
}

// Opposite of splitLines: joins the lines with '\n' between them.
string linesToText(const vector<string> &lines)
{
    string text = "";

    for (int i = 0; i < lines.size(); i++)
    {
        if (i > 0)
        {
            text += '\n';
        }

        text += lines[i];
    }

    return text;
}

// Turns a typed "\n" (backslash + n) into a real line break, so the user can
// type multi-line search and replacement text on one console line.
string unescapeLineBreaks(const string &text)
{
    string result = "";

    for (int i = 0; i < text.length(); i++)
    {
        if (text[i] == '\\' && i + 1 < text.length() && text[i + 1] == 'n')
        {
            result += '\n';
            i++;
        }
        else
        {
            result += text[i];
        }
    }

    return result;
}

// Opposite of unescapeLineBreaks: shows a line break as "\n" so one-line
// messages (status line, search list) stay on one line.
string displayText(const string &text)
{
    string result = "";

    for (int i = 0; i < text.length(); i++)
    {
        if (text[i] == '\n')
        {
            result += "\\n";
        }
        else
        {
            result += text[i];
        }
    }

    return result;
}

// One replaced piece of text (may span several lines): 'before' and 'after'
// are the exact text at line/column before and after the replacement.
// Used by REPLACE_TEXT so one Action can undo/redo many replacements.
struct LineChange
{
    int line;
    int column;
    string before;
    string after;
};

// Action class stores information about an editing operation.
class Action
{
public:
    int type;
    int line;
    int column;
    string text;

    int beforeLine;
    int beforeColumn;

    int afterLine;
    int afterColumn;

    // Only used by REPLACE_TEXT (one entry per replacement, last-in-document first).
    vector<LineChange> changes;

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

    Action(int t, int l, int c, string txt,
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

// Template-based Stack.
template <class T>
class Stack
{
private:
    T *arr;
    int top;
    int capacity;

public:
    Stack()
    {
        capacity = 10;
        top = -1;
        arr = new T[capacity];
    }

    // Copying is forbidden: the stack owns a raw array, so a copy would
    // point to the same array and delete it twice.
    Stack(const Stack &) = delete;
    Stack &operator=(const Stack &) = delete;

    void push(T value)
    {
        if (top == capacity - 1)
        {
            capacity *= 2;
            T *newArr = new T[capacity];

            for (int i = 0; i <= top; i++)
            {
                newArr[i] = arr[i];
            }

            delete[] arr;
            arr = newArr;
        }

        arr[++top] = value;
    }

    bool pop(T &value)
    {
        if (isEmpty())
        {
            return false;
        }

        value = arr[top--];
        return true;
    }

    T *peek()
    {
        if (isEmpty())
        {
            return nullptr;
        }

        return &arr[top];
    }

    bool isEmpty()
    {
        return top == -1;
    }

    void clear()
    {
        top = -1;
    }

    ~Stack()
    {
        delete[] arr;
    }
};

// Each node stores one line.
class Node
{
public:
    string data;
    Node *next;
    Node *prev;

    Node(string text)
    {
        data = text;
        next = nullptr;
        prev = nullptr;
    }
};

// Document management using a doubly linked list.
class Document
{
private:
    Node *head;
    Node *tail;
    int lineCount;
    Node *cursorLine;
    int cursorColumn;

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
            if (probeColumn < (int)probe->data.length())
            {
                char ch = probe->data[probeColumn];

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
    bool matchesFrom(Node *current, int line, int column, const string &term,
                     int &endLine, int &endColumn)
    {
        if (current == nullptr || column < 0 || column > (int)current->data.length())
        {
            return false;
        }

        for (int i = 0; i < term.length(); i++)
        {
            char wanted = tolower((unsigned char)term[i]);

            // A space (not at the start of the term) may stand for a line break.
            if (wanted == ' ' && i > 0 && skipLineBreakGap(current, line, column))
            {
                while (i + 1 < term.length() && term[i + 1] == ' ')
                {
                    i++;
                }

                continue;
            }

            if (column == (int)current->data.length())
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
                if (tolower((unsigned char)current->data[column]) != wanted)
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
        head = new Node("");
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
            total += current->data.length();
            current = current->next;
        }

        return total;
    }

    // Checks (ignoring case) whether the term is in the document at
    // line/column (see matchesFrom). Gives the line and column where the
    // match ends.
    bool matchesAt(int line, int column, const string &term,
                   int &endLine, int &endColumn)
    {
        return matchesFrom(getLineAt(line), line, column, term, endLine, endColumn);
    }

    // Finds every match (case-insensitive) of the term in the whole document,
    // line breaks included. Every position is tried, the position of a line
    // break too, so overlapping matches are found as well.
    // Returns an empty list if the term is empty or nothing matches.
    vector<SearchMatch> findAllMatches(const string &searchTerm)
    {
        vector<SearchMatch> matches;

        if (searchTerm.empty())
        {
            return matches;
        }

        int lineNumber = 0;
        Node *current = head;

        while (current != nullptr)
        {
            for (int column = 0; column <= (int)current->data.length(); column++)
            {
                int endLine, endColumn;

                if (matchesFrom(current, lineNumber, column, searchTerm,
                                endLine, endColumn))
                {
                    SearchMatch match;
                    match.line = lineNumber;
                    match.column = column;
                    matches.push_back(match);
                }
            }

            current = current->next;
            lineNumber++;
        }

        return matches;
    }

    // Counts the matches and moves the cursor to the first one.
    // (The editor itself uses findAllMatches() directly.)
    int searchText(const string &searchTerm)
    {
        vector<SearchMatch> matches = findAllMatches(searchTerm);

        if (!matches.empty())
        {
            setCursor(matches[0].line, matches[0].column);
        }

        return matches.size();
    }

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

    void setCursor(int line, int column)
    {
        Node *target = getLineAt(line);

        if (target == nullptr)
        {
            return;
        }

        cursorLine = target;

        if (column > target->data.length())
        {
            column = target->data.length();
        }

        if (column < 0)
        {
            column = 0;
        }

        cursorColumn = column;
    }

    void insertCharacterAt(int line, int column, char ch)
    {
        Node *target = getLineAt(line);

        if (target != nullptr && column >= 0 &&
            column <= target->data.length())
        {
            target->data.insert(column, 1, ch);
        }
    }

    void deleteCharacterAt(int line, int column)
    {
        Node *target = getLineAt(line);

        if (target != nullptr && column >= 0 &&
            column < target->data.length())
        {
            target->data.erase(column, 1);
        }
    }

    // Breaks one line into two at 'column'. The text after the column
    // moves into a new node that is linked in after the current line.
    void splitLineAt(int line, int column)
    {
        Node *current = getLineAt(line);

        if (current == nullptr)
        {
            return;
        }

        if (column < 0)
        {
            column = 0;
        }

        if (column > current->data.length())
        {
            column = current->data.length();
        }

        string remaining = current->data.substr(column);
        current->data.erase(column);

        Node *newNode = new Node(remaining);

        newNode->next = current->next;
        newNode->prev = current;

        if (current->next != nullptr)
        {
            current->next->prev = newNode;
        }
        else
        {
            tail = newNode;
        }

        current->next = newNode;
        lineCount++;
    }

    // Adds the next line to the end of this line and removes the next node.
    // (The column parameter is not needed, it is kept so the calls stay the same.)
    void joinLineAt(int line, int column)
    {
        Node *current = getLineAt(line);

        if (current == nullptr || current->next == nullptr)
        {
            return;
        }

        Node *nextNode = current->next;

        current->data += nextNode->data;
        current->next = nextNode->next;

        if (nextNode->next != nullptr)
        {
            nextNode->next->prev = current;
        }
        else
        {
            tail = current;
        }

        delete nextNode;
        lineCount--;
    }

    // Inserts multi-line text at line/column.
    // Example: inserting "AB\nCD" into "12|34" (cursor at |) gives
    //     12AB
    //     CD34
    // The text before the cursor joins the first inserted line and the
    // text after the cursor joins the last inserted line.
    // Also used to undo a selection deletion.
    void insertText(int line, int column, const string &text)
    {
        vector<string> lines = splitLines(text);
        int count = lines.size();
        Node *first = getLineAt(line);

        if (first == nullptr)
        {
            return;
        }

        // Keep the column inside the line so substr() can never fail.
        int length = first->data.length();

        if (column < 0)
        {
            column = 0;
        }

        if (column > length)
        {
            column = length;
        }

        string before = first->data.substr(0, column); // text left of the cursor
        string after = first->data.substr(column);     // text right of the cursor

        // One line only: just put the text in the middle.
        if (count == 1)
        {
            first->data = before + lines[0] + after;
            return;
        }

        first->data = before + lines[0];

        // Create one new line below the previous one for every extra inserted line.
        for (int i = 1; i < count; i++)
        {
            Node *previous = getLineAt(line + i - 1);

            splitLineAt(line + i - 1, previous->data.length());
            getLineAt(line + i)->data = lines[i];
        }

        // The old text that was after the cursor goes behind the last inserted line.
        getLineAt(line + count - 1)->data += after;
    }

    // Exact reverse of insertText. Also used to delete a selected range.
    // 'text' must be exactly the text that is in the document at line/column.
    void removeText(int line, int column, const string &text)
    {
        vector<string> lines = splitLines(text);
        int count = lines.size();
        Node *first = getLineAt(line);
        Node *last = getLineAt(line + count - 1);

        if (first == nullptr || last == nullptr)
        {
            return;
        }

        // Column where the removed text ends, inside the last affected line.
        int endColumn = lines[count - 1].length();

        if (count == 1)
        {
            endColumn = column + lines[0].length();
        }

        // Keep both columns inside their lines so substr() can never fail.
        if (column > first->data.length())
        {
            column = first->data.length();
        }

        if (endColumn > last->data.length())
        {
            endColumn = last->data.length();
        }

        string before = first->data.substr(0, column); // kept text before the range
        string after = last->data.substr(endColumn);   // kept text after the range

        // Join all affected lines into the first line, then replace its text
        // with just the kept parts.
        for (int i = 1; i < count; i++)
        {
            joinLineAt(line, column);
        }

        first->data = before + after;
    }

    // Replaces every occurrence of 'term' (one or more lines) in the whole
    // document. Matches are taken left to right, never overlap, and the
    // replaced text is never searched again. Replacements are made from the
    // end of the document to the start so earlier positions stay correct.
    // Every real change is added to 'changes' in that order so the caller can
    // undo it. Returns how many replacements were made.
    int replaceAll(const string &term, const string &replacement,
                   vector<LineChange> &changes)
    {
        vector<SearchMatch> matches = findAllMatches(term);
        vector<SearchMatch> chosen;
        int lastEndLine = 0;
        int lastEndColumn = 0;

        for (int i = 0; i < matches.size(); i++)
        {
            int endLine, endColumn;

            // Skip a match that starts inside the previous replaced match.
            if (!chosen.empty() && (matches[i].line < lastEndLine ||
                                    (matches[i].line == lastEndLine && matches[i].column < lastEndColumn)))
            {
                continue;
            }

            matchesAt(matches[i].line, matches[i].column, term, endLine, endColumn);
            lastEndLine = endLine;
            lastEndColumn = endColumn;
            chosen.push_back(matches[i]);
        }

        for (int i = (int)chosen.size() - 1; i >= 0; i--)
        {
            LineChange change;

            if (replaceAt(chosen[i].line, chosen[i].column, term, replacement, change))
            {
                changes.push_back(change);
            }
        }

        return chosen.size();
    }

    // Returns all lines of the document as a list of strings.
    vector<string> getLines()
    {
        vector<string> lines;
        Node *current = head;

        while (current != nullptr)
        {
            lines.push_back(current->data);
            current = current->next;
        }

        return lines;
    }

    // Replaces the match of 'term' (one or more lines) at line/column with
    // 'replacement'. 'change' receives the exact old and new text, so the
    // caller can undo it. Returns true if the text really changed.
    bool replaceAt(int line, int column, const string &term,
                   const string &replacement, LineChange &change)
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

        if (change.before == change.after)
        {
            return false;
        }

        removeText(line, column, change.before);
        insertText(line, column, replacement);
        return true;
    }

    // Returns the text from line/column up to endLine/endColumn
    // (lines separated by '\n').
    string getText(int line, int column, int endLine, int endColumn)
    {
        string text = "";
        Node *current = getLineAt(line);

        for (int i = line; i <= endLine && current != nullptr; i++)
        {
            int from = (i == line) ? column : 0;
            int to = (i == endLine) ? endColumn : (int)current->data.length();

            text += current->data.substr(from, to - from);

            if (i < endLine)
            {
                text += '\n';
            }

            current = current->next;
        }

        return text;
    }

    // Replace document contents with lines loaded from a file.
    void replaceLines(vector<string> &lines)
    {
        Node *current = head;

        while (current != nullptr)
        {
            Node *temp = current;
            current = current->next;
            delete temp;
        }

        head = nullptr;
        tail = nullptr;
        lineCount = 0;

        if (lines.empty())
        {
            head = new Node("");
            tail = head;
            lineCount = 1;
        }
        else
        {
            for (int i = 0; i < lines.size(); i++)
            {
                Node *newNode = new Node(lines[i]);

                if (head == nullptr)
                {
                    head = newNode;
                    tail = newNode;
                }
                else
                {
                    tail->next = newNode;
                    newNode->prev = tail;
                    tail = newNode;
                }

                lineCount++;
            }
        }

        cursorLine = head;
        cursorColumn = 0;
    }

    void display()
    {
        Node *current = head;

        while (current != nullptr)
        {
            cout << current->data << endl;
            current = current->next;
        }
    }

    ~Document()
    {
        Node *current = head;

        while (current != nullptr)
        {
            Node *temp = current;
            current = current->next;
            delete temp;
        }
    }
};

// Manages Undo and Redo.
class UndoRedo
{
private:
    Stack<Action> undoStack;
    Stack<Action> redoStack;

public:
    void record(Action action)
    {
        undoStack.push(action);
        redoStack.clear();
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
                action.line, action.column, action.text[0]);
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
            for (int i = (int)action.changes.size() - 1; i >= 0; i--)
            {
                LineChange &c = action.changes[i];

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
                action.line, action.column, action.text[0]);
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
                LineChange &c = action.changes[i];

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
class FileManager
{
private:
    static string folderName()
    {
        return "TextEditorFiles\\";
    }

public:
    // Creates the document folder if it does not already exist.
    static void createDocumentFolder()
    {
        CreateDirectoryA("TextEditorFiles", NULL);
    }

    // Returns true if a file or folder with this path already exists.
    static bool exists(const string &path)
    {
        return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    // Only .txt is supported. Do not silently change another extension.
    static bool hasTxtExtension(const string &filename)
    {
        return filename.length() > 4 &&
               filename.substr(filename.length() - 4) == ".txt";
    }

    // Rejects paths and Windows-invalid filename characters.
    static bool hasInvalidCharacters(const string &filename)
    {
        return filename.find_first_of("\\/:*?\"<>|") != string::npos;
    }

    // "notes.txt" becomes "TextEditorFiles\\notes.txt".
    static string buildPath(const string &filename)
    {
        return folderName() + filename;
    }

    // Save document as a normal readable text file.
    static bool saveFile(Document &document, string filename)
    {
        ofstream file(filename);

        if (!file)
        {
            return false;
        }

        Node *current = document.getLineAt(0);

        while (current != nullptr)
        {
            file << current->data << '\n';
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
    static bool openFile(Document &document, string filename, string &error)
    {
        ifstream file(filename);

        if (!file)
        {
            error = "ERROR: FILE NOT FOUND!";
            return false;
        }

        vector<string> lines;
        string line;

        while (getline(file, line))
        {
            lines.push_back(line);
        }

        if (file.bad())
        {
            error = "ERROR: COULD NOT READ FILE!";
            return false;
        }

        file.close();

        document.replaceLines(lines);
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
    // insertMergedText and removeMergedText use).
    string getText(Document &document, int startLine, int startColumn,
                   int endLine, int endColumn) const
    {
        string text = "";

        for (int i = startLine; i <= endLine; i++)
        {
            Node *node = document.getLineAt(i);

            // The first line starts at the selection start, the last line ends
            // at the selection end. Lines in between are taken completely.
            int from = 0;
            if (i == startLine)
                from = startColumn;

            int to = node->data.length();
            if (i == endLine)
                to = endColumn;

            text += node->data.substr(from, to - from);

            if (i < endLine)
            {
                text += '\n';
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
    string searchTerm;
    vector<SearchMatch> matches;
    int currentMatchIndex; // -1 means "no current match"

public:
    Search()
    {
        currentMatchIndex = -1;
    }

    // Stores the term, asks the document for all matches and selects the
    // first one. The term is stored even when nothing matches (F4 uses it).
    // An empty term is ignored: the old search stays as it was.
    bool find(Document &document, const string &term)
    {
        if (term.empty())
        {
            return false;
        }

        searchTerm = term;
        matches = document.findAllMatches(term);
        currentMatchIndex = -1;

        if (matches.empty())
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
        if (searchTerm.empty())
        {
            return false;
        }

        matches = document.findAllMatches(searchTerm);

        if (matches.empty())
        {
            currentMatchIndex = -1;
            return false;
        }

        if (currentMatchIndex < 0 || currentMatchIndex >= (int)matches.size())
        {
            currentMatchIndex = 0;
        }

        return true;
    }

    // Moves to the next match. After the last one it wraps to the first.
    // Returns false when there are no matches.
    bool nextMatch()
    {
        if (matches.empty() || currentMatchIndex < 0)
        {
            return false;
        }

        currentMatchIndex++;

        if (currentMatchIndex >= (int)matches.size())
        {
            currentMatchIndex = 0;
        }

        return true;
    }

    // Moves to the previous match. Before the first one it wraps to the last.
    // (No keyboard shortcut uses this yet.)
    bool previousMatch()
    {
        if (matches.empty() || currentMatchIndex < 0)
        {
            return false;
        }

        currentMatchIndex--;

        if (currentMatchIndex < 0)
        {
            currentMatchIndex = (int)matches.size() - 1;
        }

        return true;
    }

    // Selects one match by index. An invalid index changes nothing.
    bool setCurrentMatch(int index)
    {
        if (index < 0 || index >= (int)matches.size())
        {
            return false;
        }

        currentMatchIndex = index;
        return true;
    }

    // Forgets everything: term and matches.
    void clear()
    {
        searchTerm = "";
        matches.clear();
        currentMatchIndex = -1;
    }

    // Forgets only the matches and keeps the term. Called after the text
    // changes, because the old line/column numbers may no longer be correct.
    // (Same behavior as the old clearSearchResults().)
    void clearResults()
    {
        matches.clear();
        currentMatchIndex = -1;
    }

    bool hasMatches() const
    {
        return !matches.empty();
    }

    const vector<SearchMatch> &getMatches() const
    {
        return matches;
    }

    // Gives the line and column of the current match.
    // Returns false when there is no current match.
    bool getCurrentMatch(SearchMatch &result) const
    {
        if (currentMatchIndex < 0 || currentMatchIndex >= (int)matches.size())
        {
            return false;
        }

        result = matches[currentMatchIndex];
        return true;
    }

    const string &getSearchTerm() const
    {
        return searchTerm;
    }

    int getCurrentMatchIndex() const
    {
        return currentMatchIndex;
    }
};

// ================= EDITOR DISPLAY =================

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
    void writeConsoleRow(HANDLE console, int x, int y, string text, int width)
    {
        if (width <= 0)
            return;

        int length = text.length();

        if (length > width)
            text.resize(width);
        else if (length < width)
            text.append(width - length, ' ');

        COORD position;
        position.X = (SHORT)x;
        position.Y = (SHORT)y;

        DWORD written = 0;
        WriteConsoleOutputCharacterA(console, text.c_str(), (DWORD)width,
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
                    const string &filename, Document &document, int cursorLine)
    {
        string border(width, '=');
        string heading = "TEXT EDITOR";

        // Put the heading in the middle of the row.
        int headingStart = (width - (int)heading.length()) / 2;
        if (headingStart < 0)
            headingStart = 0;

        string headingRow(width, ' ');
        headingRow.replace(headingStart, heading.length(), heading);

        string shownName = filename;
        if (shownName.empty())
            shownName = "Untitled";

        string infoRow = " Current Line: " + to_string(cursorLine + 1) +
                         " | Column: " + to_string(document.getCursorColumn() + 1) +
                         " | Total Lines: " + to_string(document.getLineCount()) +
                         " | Characters: " + to_string(document.getCharacterCount());

        writeConsoleRow(console, left, top, border, width);
        writeConsoleRow(console, left, top + 1, headingRow, width);
        writeConsoleRow(console, left, top + 2, border, width);
        writeConsoleRow(console, left, top + 3, " File: " + shownName, width);
        writeConsoleRow(console, left, top + 4, infoRow, width);
        writeConsoleRow(console, left, top + 5, string(width, '-'), width);
    }

    // Draws the 5 rows at the bottom: shortcut help and the status message.
    void drawFooter(HANDLE console, int left, int footerTop, int width,
                    const string &status)
    {
        string lastRow = " Arrows Move | Enter New Line | Backspace/Delete Remove";
        if (!status.empty())
            lastRow = " " + status;

        writeConsoleRow(console, left, footerTop, string(width, '-'), width);
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
    static void showMessage(string message)
    {
        system("cls");
        cout << message << endl;
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

        const vector<SearchMatch> &matches = search.getMatches();
        int total = matches.size();

        system("cls");
        cout << "========== SEARCH RESULTS ==========" << endl;
        cout << "Search term: " << displayText(search.getSearchTerm()) << endl;
        cout << "Total matches: " << total << endl;
        cout << "------------------------------------" << endl;

        for (int i = 0; i < total; i++)
        {
            cout << "Match " << (i + 1) << ": Line " << (matches[i].line + 1)
                 << ", Column " << (matches[i].column + 1) << endl;

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
                string filename, const string &status)
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
        int lineNumberWidth = to_string(totalLines).length();
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
        int cursorOffset = getHorizontalOffset(document.getCursorColumn(), textWidth,
                                               cursorNode->data.length());

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
        const string &searchTerm = search.getSearchTerm();
        SearchMatch match;

        if (!searchTerm.empty() && search.getCurrentMatch(match))
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
                writeConsoleRow(console, left, screenY, "", width);
                colorConsoleRow(console, left, screenY, width,
                                normalAttribute, highlightAttribute, 0, 0);
                continue;
            }

            string number = to_string(lineIndex + 1);
            string linePrefix = " " + string(lineNumberWidth - number.length(), ' ') +
                                number + " | ";

            // lineOffset = number of hidden characters on the left of this row.
            string visibleText = current->data;
            int lineOffset = 0;

            if (lineIndex == cursorLine)
            {
                lineOffset = cursorOffset;
                visibleText = visibleText.substr(lineOffset);
            }

            writeConsoleRow(console, left, screenY, linePrefix + visibleText, width);

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

                int toColumn = current->data.length() + 1;
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

                int toColumn = current->data.length() + 1; // line break cell
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

    string filename; // "" means a new document that was never saved
    string status;   // message shown in the last row

    // ---------- Asking for file names and opening files ----------

    // Shows an error under the filename prompt and waits for a key.
    void showFilenameError(string message)
    {
        cout << message << "\n";
        cout << "Press any key to try again...";
        _getch();
        cout << "\n";
    }

    // Gets a filename and validates that the user explicitly enters .txt.
    // requireUnique is used for new documents and Save As.
    // Returns the full path, or "" when the user enters nothing.
    string getFilename(bool requireUnique = false)
    {
        string name;

        while (true)
        {
            // Use a clean prompt screen so the filename input never overlaps
            // the editor's footer or the current editing cursor.
            system("cls");
            cout << "Enter filename (example: notes.txt): ";
            getline(cin, name);

            if (name.empty())
                return "";

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

            string fullPath = FileManager::buildPath(name);

            if (requireUnique && FileManager::exists(fullPath))
            {
                showFilenameError("ERROR: FILE NAME ALREADY USED. PLEASE ENTER A UNIQUE NAME.");
                continue;
            }

            return fullPath;
        }
    }

    // Opens a file into 'target' and shows the result message to the user.
    bool loadFile(Document &target, const string &path)
    {
        string error;

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
    string matchInfoText()
    {
        return "MATCH " + to_string(search.getCurrentMatchIndex() + 1) + " OF " +
               to_string(search.getMatches().size()) + ": " + displayText(search.getSearchTerm()) +
               " (CTRL+N/F3 NEXT, F4 LIST)";
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

        status = "FOUND " + to_string(search.getMatches().size()) + " MATCHES - " +
                 matchInfoText();
    }

    // Returns the selected text (lines separated by '\n'), or "" if nothing is selected.
    string getSelectedText()
    {
        int startLine, startColumn, endLine, endColumn;

        if (!selection.getRange(document, startLine, startColumn, endLine, endColumn))
        {
            return "";
        }

        return selection.getText(document, startLine, startColumn, endLine, endColumn);
    }

    // Asks for a word or phrase and finds all matches (Ctrl+Space, F2 and Ctrl+F2).
    // Line breaks are found automatically. If text was selected, pressing Enter
    // alone searches for the selected text (it may cover several lines).
    void searchDocument(const string &selectedText)
    {
        system("cls");
        cout << "========== SEARCH DOCUMENT ==========" << endl;

        if (!selectedText.empty())
        {
            cout << "Selected text: " << displayText(selectedText) << endl;
            cout << "Press Enter to search for it, or type another word or phrase: ";
        }
        else
        {
            cout << "Enter a word or phrase: ";
        }

        string searchTerm;
        getline(cin, searchTerm);

        if (searchTerm.empty())
        {
            searchTerm = selectedText;
        }
        else
        {
            searchTerm = unescapeLineBreaks(searchTerm); // typed \n still works
        }

        if (searchTerm.empty())
        {
            status = "SEARCH CANCELLED - EMPTY SEARCH TERM";
            return;
        }

        // find() selects the first match when there is one.
        if (!search.find(document, searchTerm))
        {
            status = "NO MATCHES FOUND: " + displayText(searchTerm);
            return;
        }

        showFoundMatches();
    }

    // F4: shows the match list of the last search again. The matches are found
    // again first, so edits made since the search are included.
    void listSearchMatches()
    {
        if (search.getSearchTerm().empty())
        {
            status = "SEARCH FIRST USING CTRL+F2";
            return;
        }

        if (!search.refresh(document))
        {
            status = "NO MATCHES FOUND: " + displayText(search.getSearchTerm());
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
    bool getReplaceTerms(string title, string &searchTerm, string &replacement)
    {
        system("cls");
        cout << "========== " << title << " ==========" << endl;
        cout << "Enter a word or phrase to find (line breaks are matched automatically): ";
        getline(cin, searchTerm);
        searchTerm = unescapeLineBreaks(searchTerm);

        if (searchTerm.empty())
        {
            return false;
        }

        cout << "Enter the replacement (leave empty to delete): ";
        getline(cin, replacement);
        replacement = unescapeLineBreaks(replacement);

        return true;
    }

    // Replace One (F8): the user picks one occurrence with Up/Down,
    // Enter replaces it, Esc cancels without changing anything.
    void replaceOneOccurrence()
    {
        string searchTerm;
        string replacement;

        if (!getReplaceTerms("REPLACE ONE OCCURRENCE", searchTerm, replacement))
        {
            status = "REPLACE CANCELLED - EMPTY SEARCH TERM";
            return;
        }

        vector<SearchMatch> matches = document.findAllMatches(searchTerm);

        if (matches.empty())
        {
            status = "NO MATCHES FOUND: " + displayText(searchTerm);
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
            document.setCursor(matches[selected].line, matches[selected].column);
            preview.setCurrentMatch(selected);

            display.render(document, selection, preview, filename,
                           "FOUND " + to_string(total) + " | MATCH " +
                               to_string(selected + 1) + " OF " + to_string(total) +
                               " | UP/DOWN: MOVE | ENTER: REPLACE | ESC: CANCEL");

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

        SearchMatch match = matches[selected];

        // The document replaces the text and remembers the line before/after,
        // so undo/redo are exact.
        LineChange change;
        bool changed = document.replaceAt(match.line, match.column,
                                          searchTerm, replacement, change);

        // The new text may have several lines: the cursor goes to its end.
        vector<string> newLines = splitLines(replacement);
        int afterLine = match.line + (int)newLines.size() - 1;
        int afterColumn = newLines[newLines.size() - 1].length();

        if (newLines.size() == 1)
        {
            afterColumn += match.column;
        }

        // Undo puts the cursor back on the match, redo puts it after the new text.
        Action action(
            REPLACE_TEXT, match.line, match.column, replacement,
            match.line, match.column, afterLine, afterColumn);

        action.changes.push_back(change);

        // Nothing to record if the replacement is the same as the old text.
        if (changed)
        {
            history.record(action);
        }

        document.setCursor(afterLine, afterColumn);

        search.clearResults();

        status = "REPLACED MATCH " + to_string(selected + 1) + " OF " +
                 to_string(total) + " WITH: " + displayText(replacement);
    }

    // Replace All (F7): replaces every occurrence in one
    // undoable action.
    void replaceAllOccurrences()
    {
        string searchTerm;
        string replacement;

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
            status = "NO MATCHES FOUND: " + displayText(searchTerm);
            return;
        }

        if (!action.changes.empty())
        {
            history.record(action);
        }

        // Line lengths may have changed, so setCursor keeps the column valid.
        document.setCursor(originalLine, originalColumn);

        search.clearResults();

        status = "REPLACED " + to_string(replacementCount) + " OCCURRENCE(S) OF: " +
                 displayText(searchTerm) + " (CTRL+Z TO UNDO)";
    }

    // ---------- Merge another file (Ctrl+O) ----------

    // Merge Document (Ctrl+O): inserts another .txt file at the cursor.
    // The source file is only read, never changed, and the current document
    // keeps its filename. The merge is stored as ONE undo action.
    void mergeDocument()
    {
        string sourceName = getFilename();

        if (sourceName.empty())
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
        vector<string> lines = source.getLines();

        // An empty file is loaded as one empty line, so there is nothing to add.
        if (lines.size() == 1 && lines[0].empty())
        {
            status = "NOTHING TO MERGE - SOURCE FILE IS EMPTY";
            return;
        }

        int count = lines.size();
        string text = linesToText(lines);

        int line = document.getCursorLineIndex();
        int column = document.getCursorColumn();

        // The cursor ends right after the last inserted character.
        int afterLine = line + count - 1;
        int afterColumn = lines[count - 1].length();

        if (count == 1)
        {
            afterColumn = column + lines[0].length();
        }

        Action action(
            MERGE_TEXT, line, column, text,
            line, column, afterLine, afterColumn);

        document.insertText(line, column, text);
        history.record(action);

        document.setCursor(afterLine, afterColumn);

        search.clearResults();

        status = "MERGED " + to_string(count) + " LINE(S) FROM " + sourceName +
                 " (CTRL+Z TO UNDO)";
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

        string text = selection.getText(document, startLine, startColumn,
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
        if (filename.empty())
        {
            string newFilename = getFilename(true);

            if (!newFilename.empty())
                filename = newFilename;
        }

        if (filename.empty())
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
        string newFilename = getFilename(false);

        if (newFilename.empty())
        {
            status = "SAVE AS CANCELLED";
            return;
        }

        // If this is a different existing file, ask before replacing it.
        if (FileManager::exists(newFilename) && newFilename != filename)
        {
            system("cls");
            cout << "The file already exists:\n"
                 << newFilename << "\n\n";
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
            status = "DOCUMENT SAVED AS " + newFilename;
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

                document.setCursor(line - 1, previous->data.length());
            }
            break;

        case 77: // Right
            if (column < current->data.length())
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
            document.setCursor(line, current->data.length());
            break;
        }
    }

    // Delete key: removes the character under the cursor, or joins the next line
    // to this one when the cursor is at the end of the line.
    void deleteKeyPressed(int line, int column)
    {
        Node *current = document.getLineAt(line);

        if (column < current->data.length())
        {
            string removed(1, current->data[column]);

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

            history.record(action);

            document.joinLineAt(line, column);
        }
    }

    // Backspace key: removes the character before the cursor, or joins this
    // line to the end of the line above when the cursor is at the line start.
    void backspacePressed(int line, int column)
    {
        if (column > 0)
        {
            Node *current = document.getLineAt(line);

            string removed(1, current->data[column - 1]);

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

            int previousLength = previous->data.length();

            Action action(
                JOIN_LINES, line - 1, previousLength, "",
                line, column, line - 1, previousLength);

            history.record(action);

            document.joinLineAt(line - 1, previousLength);

            document.setCursor(line - 1, previousLength);
        }
    }

    // ---------- Reading keys (called by run) ----------

    // Arrow keys, Delete and F-keys send two codes: 0 or 224 first, then the
    // real key code. 'key' is already the second code.
    void handleExtendedKey(int key, int line, int column)
    {
        // The selection is cleared below, so keep its text for the search prompt.
        string selectedText = "";

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

            history.record(action);

            document.splitLineAt(line, column);
            document.setCursor(line + 1, 0);
            search.clearResults();

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
                    string(1, (char)key),
                    line, column, line, column + 1);

                history.record(action);

                document.insertCharacterAt(
                    line, column, (char)key);

                document.setCursor(line, column + 1);
                search.clearResults();
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

        string path = getFilename();

        if (path.empty())
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

        string choice;
        getline(cin, choice);

        if (choice == "1")
        {
            TextEditor editor;

            editor.run();
        }
        else if (choice == "2")
        {
            TextEditor editor;

            if (editor.openExistingDocument())
            {
                editor.run();
            }
        }
        else if (choice == "3")
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