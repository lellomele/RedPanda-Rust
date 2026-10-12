#include "test_keystrokes.h"
#include "qsynedit/qsynedit.h"
#include "qsynedit/keystrokes.h"
#include <QTest>

namespace QSynedit {
void TestKeyStrokes::test_recovery_content_endings_data(){
    QTest::addColumn<QString>("content");
    QTest::newRow("empty")<<QString{};
    QTest::newRow("no-newline")<<QString::fromUtf8("città 🦀");
    QTest::newRow("LF-final-empty-lines")<<QString::fromUtf8("città 🦀\nseconda\n\n");
    QTest::newRow("CRLF-final-empty-lines")<<QString::fromUtf8("città 🦀\r\nseconda\r\n\r\n");
    QTest::newRow("CR-final-empty-lines")<<QString::fromUtf8("città 🦀\rseconda\r\r");
}
void TestKeyStrokes::test_recovery_content_endings(){
    QFETCH(QString,content);QSynEdit editor;editor.setContentPreservingEndings(content);QCOMPARE(editor.text(),content);
}
void TestKeyStrokes::test_key_sequence_data()
{
    QTest::addColumn<Qt::KeyboardModifiers>("modifiers");
    QTest::addColumn<bool>("chord");
    for (bool chord : {false, true}) {
        const QByteArray prefix = chord ? "chord-" : "single-";
        QTest::newRow((prefix + "plain").constData()) << Qt::KeyboardModifiers(Qt::NoModifier) << chord;
        QTest::newRow((prefix + "control").constData()) << Qt::KeyboardModifiers(Qt::ControlModifier) << chord;
        QTest::newRow((prefix + "shift-alt").constData()) << (Qt::ShiftModifier | Qt::AltModifier) << chord;
        QTest::newRow((prefix + "meta").constData()) << Qt::KeyboardModifiers(Qt::MetaModifier) << chord;
        QTest::newRow((prefix + "keypad").constData()) << (Qt::ControlModifier | Qt::KeypadModifier) << chord;
    }
}

void TestKeyStrokes::test_key_sequence()
{
    QFETCH(Qt::KeyboardModifiers, modifiers);
    QFETCH(bool, chord);
    const int first = int(Qt::Key_K) | int(modifiers);
    const int second = int(Qt::Key_C) | int(Qt::ShiftModifier);
    QKeySequence sequence = chord ? QKeySequence(first, second) : QKeySequence(first);
    EditKeyStroke stroke;
    stroke.setKeySequence(sequence);
    QCOMPARE(stroke.key(), int(Qt::Key_K));
    QCOMPARE(stroke.keyModifiers(), modifiers);
    QCOMPARE(stroke.keySequence(), sequence);
    if (chord) {
        QCOMPARE(stroke.key2(), int(Qt::Key_C));
        QCOMPARE(stroke.keyModifiers2(), Qt::KeyboardModifiers(Qt::ShiftModifier));
    }

    EditKeyStrokes strokes;
    // Shortcut lookup deliberately ignores the keypad modifier.
    auto expected = strokes.add(EditCommand::Copy, stroke.key(), stroke.keyModifiers() & ~Qt::KeypadModifier);
    expected->setKey2(stroke.key2());
    expected->setKeyModifiers2(stroke.keyModifiers2());
    QCOMPARE(strokes.findKeySequence(sequence).get(), expected.get());
    QKeySequence unmatched = chord ? QKeySequence(first, int(Qt::Key_X)) : QKeySequence(int(Qt::Key_X));
    QVERIFY(!strokes.findKeySequence(unmatched));
}

void TestKeyStrokes::test_single_key_clears_second_key()
{
    EditKeyStroke stroke;
    QKeySequence chord(int(Qt::Key_K) | int(Qt::ControlModifier), int(Qt::Key_C));
    stroke.setKeySequence(chord);
    QKeySequence single(int(Qt::Key_V) | int(Qt::ControlModifier));
    stroke.setKeySequence(single);
    QCOMPARE(stroke.key2(), 0);
    QCOMPARE(stroke.keyModifiers2(), Qt::KeyboardModifiers(Qt::NoModifier));
    QCOMPARE(stroke.keySequence(), single);
}
}
