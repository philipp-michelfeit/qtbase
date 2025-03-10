// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include <QIdentityProxyModel>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTest>
#include <private/qapplication_p.h>
#include <private/qtablewidget_p.h>
#include <private/qtesthelpers_p.h>
#if QT_CONFIG(textmarkdownwriter)
#include <private/qtextmarkdownwriter_p.h>
#endif


using namespace QTestPrivate;

#ifdef QT_BUILD_INTERNAL
#define VERIFY_SPANS_CONSISTENCY(TEST_VIEW_) \
    QVERIFY(static_cast<QTableViewPrivate*>(QObjectPrivate::get(TEST_VIEW_))->spans.checkConsistency())
#else
#define VERIFY_SPANS_CONSISTENCY(TEST_VIEW_) (void)false
#endif

Q_DECLARE_METATYPE(Qt::Key);
Q_DECLARE_METATYPE(Qt::KeyboardModifier);
Q_DECLARE_METATYPE(QItemSelectionModel::SelectionFlag);
using BoolList = QList<bool>;
using IntList = QList<int>;
using KeyList = QList<Qt::Key>;
using SpanList = QList<QRect>;

class QtTestTableModel: public QAbstractTableModel
{
    Q_OBJECT

signals:
    void invalidIndexEncountered() const;

public slots:
    bool submit() override { ++submit_count; return QAbstractTableModel::submit(); }

public:
    QtTestTableModel(int rows = 0, int columns = 0, QObject *parent = nullptr)
        : QAbstractTableModel(parent), row_count(rows), column_count(columns)
    {}

    void insertRows(int rows)
    {
        beginInsertRows(QModelIndex(), row_count, row_count + rows - 1);
        row_count += rows;
        endInsertRows();
    }

    int rowCount(const QModelIndex& = QModelIndex()) const override
    {
        return row_count;
    }

    int columnCount(const QModelIndex& = QModelIndex()) const override
    {
        return column_count;
    }

    bool isEditable(const QModelIndex &) const { return true; }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        Qt::ItemFlags index_flags = QAbstractTableModel::flags(index);
        if (disabled_rows.contains(index.row())
            || disabled_columns.contains(index.column()))
            index_flags &= ~Qt::ItemIsEnabled;
        return index_flags;
    }

    void disableRow(int row)
    {
        disabled_rows.insert(row);
    }

    void enableRow(int row)
    {
        disabled_rows.remove(row);
    }

    void disableColumn(int column)
    {
        disabled_columns.insert(column);
    }

    void enableColumn(int column)
    {
        disabled_columns.remove(column);
    }

    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override
    {
        if (!idx.isValid() || idx.row() >= row_count || idx.column() >= column_count) {
            qWarning() << "Invalid modelIndex [%d,%d,%p]" << idx;
            emit invalidIndexEncountered();
            return QVariant();
        }

        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            return QLatin1Char('[') + QString::number(idx.row()) + QLatin1Char(',')
                + QString::number(idx.column()) + QLatin1String(",0]");
        }

        return QVariant();
    }

    bool insertRows(int start, int count, const QModelIndex &parent = QModelIndex()) override
    {
        if (start < 0 || start > row_count)
            return false;

        beginInsertRows(parent, start, start + count - 1);
        row_count += count;
        endInsertRows();
        return true;
    }

    bool removeRows(int start, int count, const QModelIndex &parent = QModelIndex()) override
    {
        if (start < 0 || start >= row_count || row_count < count)
            return false;

        beginRemoveRows(parent, start, start + count - 1);
        row_count -= count;
        endRemoveRows();
        return true;
    }

    void removeLastRow()
    {
        beginRemoveRows(QModelIndex(), row_count - 1, row_count - 1);
        --row_count;
        endRemoveRows();
    }

    void removeAllRows()
    {
        beginRemoveRows(QModelIndex(), 0, row_count - 1);
        row_count = 0;
        endRemoveRows();
    }

    bool insertColumns(int start, int count, const QModelIndex &parent = QModelIndex()) override
    {
        if (start < 0 || start > column_count)
            return false;

        beginInsertColumns(parent, start, start + count - 1);
        column_count += count;
        endInsertColumns();
        return true;
    }

    bool removeColumns(int start, int count, const QModelIndex &parent = QModelIndex()) override
    {
        if (start < 0 || start >= column_count || column_count < count)
            return false;

        beginRemoveColumns(parent, start, start + count - 1);
        column_count -= count;
        endRemoveColumns();
        return true;
    }

    void removeLastColumn()
    {
        beginRemoveColumns(QModelIndex(), column_count - 1, column_count - 1);
        --column_count;
        endRemoveColumns();
    }

    void removeAllColumns()
    {
        beginRemoveColumns(QModelIndex(), 0, column_count - 1);
        column_count = 0;
        endRemoveColumns();
    }

    bool canFetchMore(const QModelIndex &) const override
    {
        return can_fetch_more;
    }

    void fetchMore(const QModelIndex &) override
    {
        ++fetch_more_count;
    }

    QSet<int> disabled_rows;
    QSet<int> disabled_columns;
    int row_count;
    int column_count;
    int submit_count = 0;
    int fetch_more_count = 0;
    bool can_fetch_more = false;
};

class QtTestTableView : public QTableView
{
    Q_OBJECT
public:
    using QTableView::QTableView;

    void setModel(QAbstractItemModel *model) override
    {
        QTableView::setModel(model);
        connect(selectionModel(), &QItemSelectionModel::currentChanged,
                this, &QtTestTableView::slotCurrentChanged);
        connect(selectionModel(), &QItemSelectionModel::selectionChanged,
                this, &QtTestTableView::itemSelectionChanged);
        // Allow small sections in this test, since this test was made before we correctly enforced minimum sizes.
        horizontalHeader()->setMinimumSectionSize(0);
        verticalHeader()->setMinimumSectionSize(0);
    }

    void dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
                     const QList<int> &roles = QList<int>()) override
    {
        QTableView::dataChanged(topLeft, bottomRight, roles);
        QTableViewPrivate *av = static_cast<QTableViewPrivate*>(qt_widget_private(this));
        m_intersectecRect = av->intersectedRect(av->viewport->rect(), topLeft, bottomRight);
    }
    mutable QRect m_intersectecRect;

    using QTableView::moveCursor;
    using QTableView::isIndexHidden;
    using QTableView::setSelection;
    using QTableView::selectedIndexes;
    using QTableView::sizeHintForRow;
    using QTableView::initViewItemOption;

    bool checkSignalOrder = false;
public slots:
    void slotCurrentChanged(QModelIndex, QModelIndex) {
        hasCurrentChanged++;
        if (checkSignalOrder)
            QVERIFY(hasCurrentChanged > hasSelectionChanged);
    }

    void itemSelectionChanged(QItemSelection , QItemSelection ) {
        hasSelectionChanged++;
        if (checkSignalOrder)
            QVERIFY(hasCurrentChanged >= hasSelectionChanged);
    }
private:
    int hasCurrentChanged = 0;
    int hasSelectionChanged = 0;

    friend class tst_QTableView;
    friend struct QMetaTypeId<QtTestTableView::CursorAction>;
};
Q_DECLARE_METATYPE(QtTestTableView::CursorAction);

class QtTestItemDelegate : public QStyledItemDelegate
{
public:
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return hint;
    }

    QSize hint;
};

class tst_QTableView : public QObject
{
    Q_OBJECT

private:
    using CursorActionList = QList<QtTestTableView::CursorAction>;
private slots:
    void getSetCheck();

    void noDelegate();
    void noModel();
    void emptyModel();

    void removeRows_data();
    void removeRows();

    void removeColumns_data();
    void removeColumns();

    void keyboardNavigation_data();
    void keyboardNavigation();

    void headerSections_data();
    void headerSections();

    void moveCursor_data();
    void moveCursor();

    void moveCursorStrikesBack_data();
    void moveCursorStrikesBack();

    void moveCursorBiggerJump();

    void hideRows_data();
    void hideRows();

    void hideColumns_data();
    void hideColumns();

    void selection_data();
    void selection();

    void selectRow_data();
    void selectRow();

    void selectColumn_data();
    void selectColumn();

#if QT_CONFIG(shortcut)
    void selectall_data();
    void selectall();
#endif

    void visualRect_data();
    void visualRect();

    void fetchMore();
    void setHeaders();

    void resizeRowsToContents_data();
    void resizeRowsToContents();

    void resizeColumnsToContents_data();
    void resizeColumnsToContents();

    void rowViewportPosition_data();
    void rowViewportPosition();

    void rowAt_data();
    void rowAt();

    void rowHeight_data();
    void rowHeight();

    void columnViewportPosition_data();
    void columnViewportPosition();

    void columnAt_data();
    void columnAt();

    void columnWidth_data();
    void columnWidth();

    void hiddenRow_data();
    void hiddenRow();

    void hiddenColumn_data();
    void hiddenColumn();

    void sortingEnabled_data();
    void sortingEnabled();

    void sortByColumn_data();
    void sortByColumn();

    void scrollTo_data();
    void scrollTo();

    void indexAt_data();
    void indexAt();

    void span_data();
    void span();
    void spans();
    void spans_data();
    void spansAfterRowInsertion();
    void spansAfterColumnInsertion();
    void spansAfterRowRemoval();
    void spansAfterColumnRemoval();
    void editSpanFromDirections_data();
    void editSpanFromDirections();

    void checkHeaderReset();
    void checkHeaderMinSize();

    void resizeToContents();
    void resizeToContentsSpans();
    void resizeToContentsEarly();

    void tabFocus();
    void bigModel();
    void selectionSignal();
    void setCurrentIndex();

    void checkIntersectedRect_data();
    void checkIntersectedRect();

    // task-specific tests:
    void task173773_updateVerticalHeader();
    void task227953_setRootIndex();
    void task240266_veryBigColumn();
    void task248688_autoScrollNavigation();
    void task259308_scrollVerticalHeaderSwappedSections();
    void task191545_dragSelectRows();
    void taskQTBUG_5062_spansInconsistency();
    void taskQTBUG_4516_clickOnRichTextLabel();
#if QT_CONFIG(wheelevent)
    void taskQTBUG_5237_wheelEventOnHeader();
#endif
    void taskQTBUG_8585_crashForNoGoodReason();
    void taskQTBUG_7774_RtoLVisualRegionForSelection();
    void taskQTBUG_8777_scrollToSpans();
    void taskQTBUG_10169_sizeHintForRow();
    void taskQTBUG_30653_doItemsLayout();
    void taskQTBUG_50171_selectRowAfterSwapColumns();
    void deselectRow();
    void selectRowsAndCells();
    void selectColumnsAndCells();
    void selectWithHeader_data();
    void selectWithHeader();
    void resetDefaultSectionSize();

#if QT_CONFIG(wheelevent)
    void mouseWheel_data();
    void mouseWheel();
#endif

    void addColumnWhileEditing();
    void task234926_setHeaderSorting();

    void changeHeaderData();
    void viewOptions();

    void taskQTBUG_7232_AllowUserToControlSingleStep();
    void rowsInVerticalHeader();

#if QT_CONFIG(textmarkdownwriter)
    void markdownWriter();
#endif
};

// Testing get/set functions
void tst_QTableView::getSetCheck()
{
    QTableView obj1;

    obj1.setSortingEnabled(false);
    QCOMPARE(false, obj1.isSortingEnabled());
    obj1.setSortingEnabled(true);
    QCOMPARE(true, obj1.isSortingEnabled());

    obj1.setShowGrid(false);
    QCOMPARE(false, obj1.showGrid());
    obj1.setShowGrid(true);
    QCOMPARE(true, obj1.showGrid());

    obj1.setGridStyle(Qt::NoPen);
    QCOMPARE(Qt::NoPen, obj1.gridStyle());
    obj1.setGridStyle(Qt::SolidLine);
    QCOMPARE(Qt::SolidLine, obj1.gridStyle());

    obj1.setRootIndex(QModelIndex());
    QCOMPARE(QModelIndex(), obj1.rootIndex());
    QStandardItemModel model(10, 10);
    obj1.setModel(&model);
    QModelIndex index = model.index(0, 0);
    obj1.setRootIndex(index);
    QCOMPARE(index, obj1.rootIndex());

    QHeaderView *var1 = new QHeaderView(Qt::Horizontal);
    obj1.setHorizontalHeader(var1);
    QCOMPARE(var1, obj1.horizontalHeader());
    obj1.setHorizontalHeader(nullptr);
    QCOMPARE(var1, obj1.horizontalHeader());
    delete var1;

    QHeaderView *var2 = new QHeaderView(Qt::Vertical);
    obj1.setVerticalHeader(var2);
    QCOMPARE(var2, obj1.verticalHeader());
    obj1.setVerticalHeader(nullptr);
    QCOMPARE(var2, obj1.verticalHeader());
    delete var2;

    QCOMPARE(obj1.isCornerButtonEnabled(), true);
    obj1.setCornerButtonEnabled(false);
    QCOMPARE(obj1.isCornerButtonEnabled(), false);
}
void tst_QTableView::noDelegate()
{
    QtTestTableModel model(3, 3);
    QTableView view;
    view.setModel(&model);
    view.setItemDelegate(nullptr);
    view.show();
}

void tst_QTableView::noModel()
{
    QTableView view;
    view.show();
}

void tst_QTableView::emptyModel()
{
    QtTestTableModel model;
    QTableView view;
    QSignalSpy spy(&model, &QtTestTableModel::invalidIndexEncountered);
    view.setModel(&model);
    view.show();
    QCOMPARE(spy.size(), 0);
}

void tst_QTableView::removeRows_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("columnCount");

    QTest::newRow("2x2") << 2 << 2;
    QTest::newRow("10x10") << 10  << 10;
}

void tst_QTableView::removeRows()
{
    QFETCH(int, rowCount);
    QFETCH(int, columnCount);

    QtTestTableModel model(rowCount, columnCount);
    QSignalSpy spy(&model, &QtTestTableModel::invalidIndexEncountered);

    QTableView view;
    view.setModel(&model);
    view.show();

    model.removeLastRow();
    QCOMPARE(spy.size(), 0);

    model.removeAllRows();
    QCOMPARE(spy.size(), 0);
}

void tst_QTableView::removeColumns_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("columnCount");

    QTest::newRow("2x2") << 2 << 2;
    QTest::newRow("10x10") << 10  << 10;
}

void tst_QTableView::removeColumns()
{
    QFETCH(int, rowCount);
    QFETCH(int, columnCount);

    QtTestTableModel model(rowCount, columnCount);
    QSignalSpy spy(&model, &QtTestTableModel::invalidIndexEncountered);

    QTableView view;
    view.setModel(&model);
    view.show();

    model.removeLastColumn();
    QCOMPARE(spy.size(), 0);

    model.removeAllColumns();
    QCOMPARE(spy.size(), 0);
}

void tst_QTableView::keyboardNavigation_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("columnCount");
    QTest::addColumn<bool>("tabKeyNavigation");
    QTest::addColumn<KeyList>("keyPresses");

    const KeyList keyList {
            Qt::Key_Up,   Qt::Key_Up,   Qt::Key_Right, Qt::Key_Right,
            Qt::Key_Up,   Qt::Key_Left, Qt::Key_Left,  Qt::Key_Up,
            Qt::Key_Down, Qt::Key_Up,   Qt::Key_Up,    Qt::Key_Up,
            Qt::Key_Up,   Qt::Key_Up,   Qt::Key_Up,    Qt::Key_Left,
            Qt::Key_Left, Qt::Key_Up,   Qt::Key_Down,  Qt::Key_Down,
            Qt::Key_Tab,  Qt::Key_Backtab};

    QTest::newRow("16x16 model") << 16  << 16 << true << keyList;
    QTest::newRow("no tab") << 8  << 8 <<  false << keyList;
}

void tst_QTableView::keyboardNavigation()
{
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"), Qt::CaseInsensitive))
        QSKIP("Wayland: This fails. Figure out why.");

    QFETCH(int, rowCount);
    QFETCH(int, columnCount);
    QFETCH(bool, tabKeyNavigation);
    QFETCH(const KeyList, keyPresses);

    QtTestTableModel model(rowCount, columnCount);
    QTableView view;
    view.setModel(&model);

    view.setTabKeyNavigation(tabKeyNavigation);
    QModelIndex index = model.index(rowCount - 1, columnCount - 1);
    view.setCurrentIndex(index);

    view.show();
    QApplicationPrivate::setActiveWindow(&view);
    QVERIFY(QTest::qWaitForWindowActive(&view));

    int row = rowCount - 1;
    int column = columnCount - 1;
    for (Qt::Key key : keyPresses) {

        switch (key) {
        case Qt::Key_Up:
            row = qMax(0, row - 1);
            break;
        case Qt::Key_Down:
            row = qMin(rowCount - 1, row + 1);
            break;
        case Qt::Key_Backtab:
            if (!tabKeyNavigation)
                break;
            Q_FALLTHROUGH();
        case Qt::Key_Left:
            column = qMax(0, column - 1);
            break;
        case Qt::Key_Tab:
            if (!tabKeyNavigation)
                break;
            Q_FALLTHROUGH();
        case Qt::Key_Right:
            column = qMin(columnCount - 1, column + 1);
            break;
        default:
            break;
        }

        QTest::keyClick(&view, key);
        QApplication::processEvents();

        QModelIndex index = model.index(row, column);
        QCOMPARE(view.currentIndex(), index);
    }
}

void tst_QTableView::headerSections_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("columnCount");
    QTest::addColumn<int>("row");
    QTest::addColumn<int>("column");
    QTest::addColumn<int>("rowHeight");
    QTest::addColumn<int>("columnWidth");

    QTest::newRow("") << 10 << 10 << 5 << 5 << 30 << 30;
}

void tst_QTableView::headerSections()
{
    QFETCH(int, rowCount);
    QFETCH(int, columnCount);
    QFETCH(int, row);
    QFETCH(int, column);
    QFETCH(int, rowHeight);
    QFETCH(int, columnWidth);

    QtTestTableModel model(rowCount, columnCount);

    QTableView view;
    QHeaderView *hheader = view.horizontalHeader();
    QHeaderView *vheader = view.verticalHeader();

    view.setModel(&model);
    hheader->setMinimumSectionSize(columnWidth);
    vheader->setMinimumSectionSize(rowHeight);
    view.show();

    hheader->doItemsLayout();
    vheader->doItemsLayout();

    QCOMPARE(hheader->count(), model.columnCount());
    QCOMPARE(vheader->count(), model.rowCount());

    view.setRowHeight(row, rowHeight);
    QCOMPARE(view.rowHeight(row), rowHeight);
    view.hideRow(row);
    QCOMPARE(view.rowHeight(row), 0);
    view.showRow(row);
    QCOMPARE(view.rowHeight(row), rowHeight);

    view.setColumnWidth(column, columnWidth);
    QCOMPARE(view.columnWidth(column), columnWidth);
    view.hideColumn(column);
    QCOMPARE(view.columnWidth(column), 0);
    view.showColumn(column);
    QCOMPARE(view.columnWidth(column), columnWidth);
}

typedef QPair<int,int> IntPair;

void tst_QTableView::moveCursor_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("columnCount");
    QTest::addColumn<int>("hideRow");
    QTest::addColumn<int>("hideColumn");

    QTest::addColumn<int>("startRow");
    QTest::addColumn<int>("startColumn");

    QTest::addColumn<QtTestTableView::CursorAction>("cursorMoveAction");
    QTest::addColumn<Qt::KeyboardModifier>("modifier");

    QTest::addColumn<int>("expectedRow");
    QTest::addColumn<int>("expectedColumn");
    QTest::addColumn<IntPair>("moveRow");
    QTest::addColumn<IntPair>("moveColumn");

    // MoveRight
    QTest::newRow("MoveRight (0,0)")
        << 4 << 4 << -1 << -1
        << 0 << 0
        << QtTestTableView::MoveRight << Qt::NoModifier
        << 0 << 1 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveRight (3,0)")
        << 4 << 4 << -1 << -1
        << 3 << 0
        << QtTestTableView::MoveRight << Qt::NoModifier
        << 3 << 1 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveRight (3,3)")
        << 4 << 4 << -1 << -1
        << 3 << 3
        << QtTestTableView::MoveRight << Qt::NoModifier
        << 3 << 3 << IntPair(0,0) << IntPair(0,0); // ###

    QTest::newRow("MoveRight, hidden column 1 (0,0)")
        << 4 << 4 << -1 << 1
        << 0 << 0
        << QtTestTableView::MoveRight << Qt::NoModifier
        << 0 << 2 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveRight, hidden column 3 (0,2)")
        << 4 << 4 << -1 << 3
        << 0 << 2
        << QtTestTableView::MoveRight << Qt::NoModifier
        << 0 << 2 << IntPair(0,0) << IntPair(0,0); // ###

    // MoveNext should in addition wrap
    QTest::newRow("MoveNext (0,0)")
        << 4 << 4 << -1 << -1
        << 0 << 0
        << QtTestTableView::MoveNext << Qt::NoModifier
        << 0 << 1 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveNext (0,2)")
        << 4 << 4 << -1 << -1
        << 0 << 2
        << QtTestTableView::MoveNext << Qt::NoModifier
        << 0 << 3 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveNext, wrap (0,3)")
        << 4 << 4 << -1 << -1
        << 0 << 3
        << QtTestTableView::MoveNext << Qt::NoModifier
        << 1 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveNext, wrap (3,3)")
        << 4 << 4 << -1 << -1
        << 3 << 3
        << QtTestTableView::MoveNext << Qt::NoModifier
        << 0 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveNext, hidden column 1 (0,0)")
        << 4 << 4 << -1 << 1
        << 0 << 0
        << QtTestTableView::MoveNext << Qt::NoModifier
        << 0 << 2 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveNext, wrap, hidden column 3 (0,2)")
        << 4 << 4 << -1 << 3
        << 0 << 2
        << QtTestTableView::MoveNext << Qt::NoModifier
        << 1 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveNext, wrap, hidden column 3 (3,2)")
        << 4 << 4 << -1 << 3
        << 3 << 2
        << QtTestTableView::MoveNext << Qt::NoModifier
        << 0 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveNext, wrapy, wrapx, hidden column 3, hidden row 3 (2,2)")
        << 4 << 4 << 3 << 3
        << 2 << 2
        << QtTestTableView::MoveNext << Qt::NoModifier
        << 0 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveNext, wrap, hidden column 2, moved column from 3 to 0. (0,2)")
        << 4 << 4 << -1 << 2
        << 0 << 2
        << QtTestTableView::MoveNext << Qt::NoModifier
        << 1 << 3 << IntPair(0,0) << IntPair(3,0);

    // MoveLeft
    QTest::newRow("MoveLeft (0,0)")
        << 4 << 4 << -1 << -1
        << 0 << 0
        << QtTestTableView::MoveLeft << Qt::NoModifier
        << 0 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveLeft (0,3)")
        << 4 << 4 << -1 << -1
        << 0 << 3
        << QtTestTableView::MoveLeft << Qt::NoModifier
        << 0 << 2 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveLeft (1,0)")
        << 4 << 4 << -1 << -1
        << 1 << 0
        << QtTestTableView::MoveLeft << Qt::NoModifier
        << 1 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveLeft, hidden column 0 (0,2)")
        << 4 << 4 << -1 << 1
        << 0 << 2
        << QtTestTableView::MoveLeft << Qt::NoModifier
        << 0 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveLeft, hidden column 0 (0,1)")
        << 4 << 4 << -1 << 0
        << 0 << 1
        << QtTestTableView::MoveLeft << Qt::NoModifier
        << 0 << 1 << IntPair(0,0) << IntPair(0,0);

    // MovePrevious should in addition wrap
    QTest::newRow("MovePrevious (0,3)")
        << 4 << 4 << -1 << -1
        << 0 << 3
        << QtTestTableView::MovePrevious << Qt::NoModifier
        << 0 << 2 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MovePrevious (0,1)")
        << 4 << 4 << -1 << -1
        << 0 << 1
        << QtTestTableView::MovePrevious << Qt::NoModifier
        << 0 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MovePrevious, wrap (1,0)")
        << 4 << 4 << -1 << -1
        << 1 << 0
        << QtTestTableView::MovePrevious << Qt::NoModifier
        << 0 << 3 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MovePrevious, wrap, (0,0)")
        << 4 << 4 << -1 << -1
        << 0 << 0
        << QtTestTableView::MovePrevious << Qt::NoModifier
        << 3 << 3 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MovePrevious, hidden column 1 (0,2)")
        << 4 << 4 << -1 << 1
        << 0 << 2
        << QtTestTableView::MovePrevious << Qt::NoModifier
        << 0 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MovePrevious, wrap, hidden column 3 (0,2)")
        << 4 << 4 << -1 << 3
        << 0 << 2
        << QtTestTableView::MovePrevious << Qt::NoModifier
        << 0 << 1 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MovePrevious, wrapy, hidden column 0 (0,1)")
        << 4 << 4 << -1 << 0
        << 0 << 1
        << QtTestTableView::MovePrevious << Qt::NoModifier
        << 3 << 3 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MovePrevious, wrap, hidden column 0, hidden row 0 (1,1)")
        << 4 << 4 << 0 << 0
        << 1 << 1
        << QtTestTableView::MovePrevious << Qt::NoModifier
        << 3 << 3 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MovePrevious, wrap, hidden column 1, moved column from 0 to 3. (1,2)")
        << 4 << 4 << -1 << 1
        << 1 << 2
        << QtTestTableView::MovePrevious << Qt::NoModifier
        << 0 << 0 << IntPair(0,0) << IntPair(0,3);

    // MoveDown
    QTest::newRow("MoveDown (0,0)")
        << 4 << 4 << -1 << -1
        << 0 << 0
        << QtTestTableView::MoveDown << Qt::NoModifier
        << 1 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveDown (3,0)")
        << 4 << 4 << -1 << -1
        << 3 << 0
        << QtTestTableView::MoveDown << Qt::NoModifier
        << 3 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveDown (3,3)")
        << 4 << 4 << -1 << -1
        << 3 << 3
        << QtTestTableView::MoveDown << Qt::NoModifier
        << 3 << 3 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveDown, hidden row 1 (0,0)")
        << 4 << 4 << 1 << -1
        << 0 << 0
        << QtTestTableView::MoveDown << Qt::NoModifier
        << 2 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveDown, hidden row 3 (2,0)")
        << 4 << 4 << 3 << -1
        << 2 << 0
        << QtTestTableView::MoveDown << Qt::NoModifier
        << 2 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveDown, hidden row 0 hidden column 0 (0,0)")
        << 4 << 4 << 0 << 0
        << 0 << 0
        << QtTestTableView::MoveDown << Qt::NoModifier
        << 1 << 1 << IntPair(0,0) << IntPair(0,0);

    // MoveUp
    QTest::newRow("MoveUp (0,0)")
        << 4 << 4 << -1 << -1
        << 0 << 0
        << QtTestTableView::MoveUp << Qt::NoModifier
        << 0 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveUp (3, 0)")
        << 4 << 4 << -1 << -1
        << 3 << 0
        << QtTestTableView::MoveUp << Qt::NoModifier
        << 2 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveUp (0,1)")
        << 4 << 4 << -1 << -1
        << 0 << 1
        << QtTestTableView::MoveUp << Qt::NoModifier
        << 0 << 1 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveUp, hidden row 1 (2,0)")
        << 4 << 4 << 1 << -1
        << 2 << 0
        << QtTestTableView::MoveUp << Qt::NoModifier
        << 0 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveUp, hidden row (1,0)")
        << 4 << 4 << 0 << -1
        << 1 << 0
        << QtTestTableView::MoveUp << Qt::NoModifier
        << 1 << 0 << IntPair(0,0) << IntPair(0,0);

    // MoveHome
    QTest::newRow("MoveHome (0,0)")
        << 4 << 4 << -1 << -1
        << 0 << 0
        << QtTestTableView::MoveHome << Qt::NoModifier
        << 0 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveHome (3,3)")
        << 4 << 4 << -1 << -1
        << 3 << 3
        << QtTestTableView::MoveHome << Qt::NoModifier
        << 3 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveHome, hidden column 0 (3,3)")
        << 4 << 4 << -1 << 0
        << 3 << 3
        << QtTestTableView::MoveHome << Qt::NoModifier
        << 3 << 1 << IntPair(0,0) << IntPair(0,0);

    // Use Ctrl modifier
    QTest::newRow("MoveHome + Ctrl (0,0)")
        << 4 << 4 << -1 << -1
        << 0 << 0
        << QtTestTableView::MoveHome << Qt::ControlModifier
        << 0 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveHome + Ctrl (3,3)")
        << 4 << 4 << -1 << -1
        << 3 << 3
        << QtTestTableView::MoveHome << Qt::ControlModifier
        << 0 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveHome + Ctrl, hidden column 0, hidden row 0 (3,3)")
        << 4 << 4 << 0 << 0
        << 3 << 3
        << QtTestTableView::MoveHome << Qt::ControlModifier
        << 1 << 1 << IntPair(0,0) << IntPair(0,0);

    // MoveEnd
    QTest::newRow("MoveEnd (0,0)")
        << 4 << 4 << -1 << -1
        << 0 << 0
        << QtTestTableView::MoveEnd << Qt::NoModifier
        << 0 << 3 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveEnd (3,3)")
        << 4 << 4 << -1 << -1
        << 3 << 3
        << QtTestTableView::MoveEnd << Qt::NoModifier
        << 3 << 3 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveEnd, hidden column (0,0)")
        << 4 << 4 << -1 << 3
        << 0 << 0
        << QtTestTableView::MoveEnd << Qt::NoModifier
        << 0<< 2 << IntPair(0,0) << IntPair(0,0);

    // Use Ctrl modifier
    QTest::newRow("MoveEnd + Ctrl (0,0)")
        << 4 << 4 << -1 << -1
        << 0 << 0
        << QtTestTableView::MoveEnd << Qt::ControlModifier
        << 3 << 3 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveEnd + Ctrl (3,3)")
        << 4 << 4 << -1 << -1
        << 3 << 3
        << QtTestTableView::MoveEnd << Qt::ControlModifier
        << 3 << 3 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveEnd + Ctrl, hidden column 3 (0,0)")
        << 4 << 4 << -1 << 3
        << 0 << 0
        << QtTestTableView::MoveEnd << Qt::ControlModifier
        << 3 << 2 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MoveEnd + Ctrl, hidden column 3, hidden row 3 (0,0)")
        << 4 << 4 << 3 << 3
        << 0 << 0
        << QtTestTableView::MoveEnd << Qt::ControlModifier
        << 2 << 2 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MovePageUp (0,0)")
        << 4 << 4 << -1 << -1
        << 0 << 0
        << QtTestTableView::MovePageUp << Qt::NoModifier
        << 0 << 0 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MovePageUp (3,3)")
        << 4 << 4 << -1 << -1
        << 3 << 3
        << QtTestTableView::MovePageUp << Qt::NoModifier
        << 0 << 3 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MovePageDown (3, 3)")
        << 4 << 4 << -1 << -1
        << 3 << 3
        << QtTestTableView::MovePageDown << Qt::NoModifier
        << 3 << 3 << IntPair(0,0) << IntPair(0,0);

    QTest::newRow("MovePageDown (0, 3)")
        << 4 << 4 << -1 << -1
        << 0 << 3
        << QtTestTableView::MovePageDown << Qt::NoModifier
        << 3 << 3 << IntPair(0,0) << IntPair(0,0);
}

void tst_QTableView::moveCursor()
{
    QFETCH(int, rowCount);
    QFETCH(int, columnCount);
    QFETCH(int, hideRow);
    QFETCH(int, hideColumn);
    QFETCH(int, startRow);
    QFETCH(int, startColumn);
    QFETCH(QtTestTableView::CursorAction, cursorMoveAction);
    QFETCH(Qt::KeyboardModifier, modifier);
    QFETCH(int, expectedRow);
    QFETCH(int, expectedColumn);
    QFETCH(IntPair, moveRow);
    QFETCH(IntPair, moveColumn);

    QtTestTableModel model(rowCount, columnCount);
    QtTestTableView view;

    view.setModel(&model);
    // we have to make sure that PgUp/PgDown can scroll to the bottom/top
    view.resize(view.horizontalHeader()->length() + 50,
                view.verticalHeader()->length() + 50);
    view.hideRow(hideRow);
    view.hideColumn(hideColumn);
    if (moveColumn.first != moveColumn.second)
        view.horizontalHeader()->moveSection(moveColumn.first, moveColumn.second);
    if (moveRow.first != moveRow.second)
        view.verticalHeader()->moveSection(moveRow.first, moveRow.second);

    view.show();

    QModelIndex index = model.index(startRow, startColumn);
    view.setCurrentIndex(index);

    QModelIndex newIndex = view.moveCursor(cursorMoveAction, modifier);
    // expected fails, task 119433
    if(newIndex.row() == -1)
        return;
    QCOMPARE(newIndex.row(), expectedRow);
    QCOMPARE(newIndex.column(), expectedColumn);
}

void tst_QTableView::moveCursorStrikesBack_data()
{
    QTest::addColumn<int>("hideRow");
    QTest::addColumn<int>("hideColumn");
    QTest::addColumn<IntList>("disableRows");
    QTest::addColumn<IntList>("disableColumns");
    QTest::addColumn<QRect>("span");

    QTest::addColumn<int>("startRow");
    QTest::addColumn<int>("startColumn");
    QTest::addColumn<CursorActionList>("cursorMoveActions");
    QTest::addColumn<int>("expectedRow");
    QTest::addColumn<int>("expectedColumn");

    QTest::newRow("Last column disabled. Task QTBUG-3878") << -1 << -1
            << IntList()
            << (IntList() << 6)
            << QRect()
            << 0 << 5
            << CursorActionList{QtTestTableView::MoveNext}
            << 1 << 0;

    QTest::newRow("Last column disabled 2. Task QTBUG-3878") << -1 << -1
            << IntList()
            << (IntList() << 6)
            << QRect()
            << 1 << 0
            << CursorActionList{QtTestTableView::MovePrevious}
            << 0 << 5;

    QTest::newRow("Span, anchor column hidden") << -1 << 1
            << IntList()
            << IntList()
            << QRect(1, 2, 2, 3)
            << 2 << 0
            << CursorActionList{QtTestTableView::MoveNext}
            << 2 << 1;

    QTest::newRow("Span, anchor column disabled") << -1 << -1
            << IntList()
            << (IntList() << 1)
            << QRect(1, 2, 2, 3)
            << 2 << 0
            << CursorActionList{QtTestTableView::MoveNext}
            << 2 << 1;

    QTest::newRow("Span, anchor row hidden") << 2 << -1
            << IntList()
            << IntList()
            << QRect(1, 2, 2, 3)
            << 1 << 2
            << CursorActionList{QtTestTableView::MoveDown}
            << 2 << 1;

    QTest::newRow("Span, anchor row disabled") << -1 << -1
            << (IntList() << 2)
            << IntList()
            << QRect(1, 2, 2, 3)
            << 1 << 2
            << CursorActionList{QtTestTableView::MoveDown}
            << 2 << 1;

    QTest::newRow("Move through span right") << -1 << -1
            << IntList()
            << IntList()
            << QRect(1, 2, 2, 3)
            << 3 << 0
            << CursorActionList{QtTestTableView::MoveRight,
                                QtTestTableView::MoveRight}
            << 3 << 3;

    QTest::newRow("Move through span left") << -1 << -1
            << IntList()
            << IntList()
            << QRect(1, 2, 2, 3)
            << 3 << 3
            << CursorActionList{QtTestTableView::MoveLeft,
                                QtTestTableView::MoveLeft}
            << 3 << 0;

    QTest::newRow("Move through span down") << -1 << -1
            << IntList()
            << IntList()
            << QRect(1, 2, 2, 3)
            << 1 << 2
            << CursorActionList{QtTestTableView::MoveDown,
                                QtTestTableView::MoveDown}
            << 5 << 2;

    QTest::newRow("Move through span up") << -1 << -1
            << IntList()
            << IntList()
            << QRect(1, 2, 2, 3)
            << 5 << 2
            << CursorActionList{QtTestTableView::MoveUp,
                                QtTestTableView::MoveUp}
            << 1 << 2;

    IntList fullList;
    for (int i = 0; i < 7; ++i)
        fullList << i;

    QTest::newRow("All disabled, wrap forward. => invalid index") << -1 << -1
            << fullList
            << fullList
            << QRect()
            << 1 << 0
            << CursorActionList{QtTestTableView::MoveNext}
            << -1 << -1;

    QTest::newRow("All disabled, wrap backwards. => invalid index") << -1 << -1
            << fullList
            << fullList
            << QRect()
            << 1 << 0
            << CursorActionList{QtTestTableView::MovePrevious}
            << -1 << -1;

    QTest::newRow("Last column disabled, MoveEnd. QTBUG-72400") << -1 << -1
            << IntList()
            << (IntList() << 6)
            << QRect()
            << 0 << 0
            << CursorActionList{QtTestTableView::MoveEnd}
            << 0 << 5;

    QTest::newRow("First column disabled, MoveHome. QTBUG-72400") << -1 << -1
            << IntList()
            << (IntList() << 0)
            << QRect()
            << 0 << 6
            << CursorActionList{QtTestTableView::MoveHome}
            << 0 << 1;

    QTest::newRow("First row disabled, MovePageUp. QTBUG-72400") << -1 << -1
            << (IntList() << 0)
            << IntList()
            << QRect()
            << 2 << 0
            << CursorActionList{QtTestTableView::MovePageUp}
            << 1 << 0;

    QTest::newRow("Last row disabled, MovePageDown. QTBUG-72400") << -1 << -1
            << (IntList() << 6)
            << IntList()
            << QRect()
            << 4 << 0
            << CursorActionList{QtTestTableView::MovePageDown}
            << 5 << 0;
}

void tst_QTableView::moveCursorStrikesBack()
{
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"), Qt::CaseInsensitive))
        QSKIP("Wayland: This fails. Figure out why.");

    QFETCH(int, hideRow);
    QFETCH(int, hideColumn);
    QFETCH(const IntList, disableRows);
    QFETCH(const IntList, disableColumns);
    QFETCH(QRect, span);

    QFETCH(int, startRow);
    QFETCH(int, startColumn);
    QFETCH(const CursorActionList, cursorMoveActions);
    QFETCH(int, expectedRow);
    QFETCH(int, expectedColumn);

    QtTestTableModel model(7, 7);
    QtTestTableView view;
    view.setModel(&model);
    view.hideRow(hideRow);
    view.hideColumn(hideColumn);

    if (span.height() && span.width())
        view.setSpan(span.top(), span.left(), span.height(), span.width());
    view.show();
    QVERIFY(QTest::qWaitForWindowActive(&view));
    // resize to make sure there are scrollbars
    view.resize(view.columnWidth(0) * 7, view.rowHeight(0) * 7);

    QModelIndex index = model.index(startRow, startColumn);
    view.setCurrentIndex(index);

    for (int row : disableRows)
        model.disableRow(row);
    for (int column : disableColumns)
        model.disableColumn(column);

    int newRow = -1;
    int newColumn = -1;
    for (auto cursorMoveAction : cursorMoveActions) {
        QModelIndex newIndex = view.moveCursor(cursorMoveAction, {});
        view.setCurrentIndex(newIndex);
        newRow = newIndex.row();
        newColumn = newIndex.column();
    }

    QCOMPARE(newRow, expectedRow);
    QCOMPARE(newColumn, expectedColumn);
}

void tst_QTableView::moveCursorBiggerJump()
{
    QtTestTableModel model(50, 7);
    QTableView view;
    view.setModel(&model);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    int height = view.horizontalHeader()->height();
    for (int i=0;i<8;i++)
        height += view.verticalHeader()->sectionSize(i);
    view.resize(view.width(), height);
    view.setCurrentIndex(model.index(0,0));

    QTest::keyClick(&view, Qt::Key_PageDown);
    QCOMPARE(view.indexAt(QPoint(0,0)), model.index(1,0));
    QTest::keyClick(&view, Qt::Key_PageDown);
    QCOMPARE(view.indexAt(QPoint(0,0)), model.index(8,0));
    QTest::keyClick(&view, Qt::Key_PageDown);
    QCOMPARE(view.indexAt(QPoint(0,0)), model.index(15,0));
    QTest::keyClick(&view, Qt::Key_PageUp);
    QCOMPARE(view.indexAt(QPoint(0,0)), model.index(14,0));
    QTest::keyClick(&view, Qt::Key_PageUp);
    QCOMPARE(view.indexAt(QPoint(0,0)), model.index(7,0));
    QTest::keyClick(&view, Qt::Key_PageUp);
    QCOMPARE(view.indexAt(QPoint(0,0)), model.index(0,0));

    QTest::keyClick(&view, Qt::Key_PageDown);
    view.verticalHeader()->hideSection(0);
    QTest::keyClick(&view, Qt::Key_PageUp);
    QTRY_COMPARE(view.currentIndex().row(), view.rowAt(0));
}

void tst_QTableView::hideRows_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("columnCount");
    QTest::addColumn<int>("showRow"); // hide, then show
    QTest::addColumn<int>("hideRow"); // hide only
    QTest::addColumn<int>("row");
    QTest::addColumn<int>("column");
    QTest::addColumn<int>("rowSpan");
    QTest::addColumn<int>("columnSpan");

    QTest::newRow("show row 0, hide row 3, no span")
      << 10 << 10
      << 0
      << 3
      << -1 << -1
      << 1 << 1;

    QTest::newRow("show row 0, hide row 3, span")
      << 10 << 10
      << 0
      << 3
      << 0 << 0
      << 3 << 2;
}

void tst_QTableView::hideRows()
{
    QFETCH(int, rowCount);
    QFETCH(int, columnCount);
    QFETCH(int, showRow);
    QFETCH(int, hideRow);
    QFETCH(int, row);
    QFETCH(int, column);
    QFETCH(int, rowSpan);
    QFETCH(int, columnSpan);

    QtTestTableModel model(rowCount, columnCount);
    QTableView view;

    view.setModel(&model);
    view.setSpan(row, column, rowSpan, columnSpan);

    view.hideRow(showRow);
    QVERIFY(view.isRowHidden(showRow));

    view.hideRow(hideRow);
    QVERIFY(view.isRowHidden(hideRow));

    view.showRow(showRow);
    QVERIFY(!view.isRowHidden(showRow));
    QVERIFY(view.isRowHidden(hideRow));
}

void tst_QTableView::hideColumns_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("columnCount");
    QTest::addColumn<int>("showColumn"); // hide, then show
    QTest::addColumn<int>("hideColumn"); // hide only
    QTest::addColumn<int>("row");
    QTest::addColumn<int>("column");
    QTest::addColumn<int>("rowSpan");
    QTest::addColumn<int>("columnSpan");

    QTest::newRow("show col 0, hide col 3, no span")
      << 10 << 10
      << 0
      << 3
      << -1 << -1
      << 1 << 1;

    QTest::newRow("show col 0, hide col 3, span")
      << 10 << 10
      << 0
      << 3
      << 0 << 0
      << 3 << 2;
}

void tst_QTableView::hideColumns()
{
    QFETCH(int, rowCount);
    QFETCH(int, columnCount);
    QFETCH(int, showColumn);
    QFETCH(int, hideColumn);
    QFETCH(int, row);
    QFETCH(int, column);
    QFETCH(int, rowSpan);
    QFETCH(int, columnSpan);

    QtTestTableModel model(rowCount, columnCount);

    QTableView view;
    view.setModel(&model);
    view.setSpan(row, column, rowSpan, columnSpan);

    view.hideColumn(showColumn);
    QVERIFY(view.isColumnHidden(showColumn));

    view.hideColumn(hideColumn);
    QVERIFY(view.isColumnHidden(hideColumn));

    view.showColumn(showColumn);
    QVERIFY(!view.isColumnHidden(showColumn));
    QVERIFY(view.isColumnHidden(hideColumn));
}

void tst_QTableView::selection_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("columnCount");
    QTest::addColumn<int>("row");
    QTest::addColumn<int>("column");
    QTest::addColumn<int>("rowSpan");
    QTest::addColumn<int>("columnSpan");
    QTest::addColumn<int>("hideRow");
    QTest::addColumn<int>("hideColumn");
    QTest::addColumn<int>("moveRowFrom");
    QTest::addColumn<int>("moveRowTo");
    QTest::addColumn<int>("moveColumnFrom");
    QTest::addColumn<int>("moveColumnTo");
    QTest::addColumn<int>("rowHeight");
    QTest::addColumn<int>("columnWidth");
    QTest::addColumn<int>("x");
    QTest::addColumn<int>("y");
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("height");
    QTest::addColumn<QItemSelectionModel::SelectionFlag>("command");
    QTest::addColumn<int>("selectedCount"); // ### make this more detailed

    QTest::newRow("no span, no hidden, no moved, 3x3 select")
      << 10 << 10                          // dim
      << -1 << -1                          // pos
      << 1 << 1                            // span
      << -1 << -1                          // hide
      << -1 << -1                          // move row
      << -1 << -1                          // move col
      << 40 << 40                          // cell size
      << 20 << 20 << 80 << 80              // rect
      << QItemSelectionModel::Select       // command
      << 9;                                // selected count

    QTest::newRow("row span, no hidden, no moved, 3x3 select")
      << 10 << 10                          // dim
      << 1 << 1                            // pos
      << 2 << 1                            // span
      << -1 << -1                          // hide
      << -1 << -1                          // move row
      << -1 << -1                          // move col
      << 40 << 40                          // cell size
      << 20 << 20 << 80 << 80              // rect
      << QItemSelectionModel::Select       // command
      << 8;                                // selected count

    QTest::newRow("col span, no hidden, no moved, 3x3 select")
      << 10 << 10                          // dim
      << 1 << 1                            // pos
      << 1 << 2                            // span
      << -1 << -1                          // hide
      << -1 << -1                          // move row
      << -1 << -1                          // move col
      << 40 << 40                          // cell size
      << 20 << 20 << 80 << 80              // rect
      << QItemSelectionModel::Select       // command
      << 8;                                // selected count

    QTest::newRow("no span, row hidden, no moved, 3x3 select")
      << 10 << 10                          // dim
      << -1 << -1                          // pos
      << 1 << 1                            // span
      << 1 << -1                           // hide
      << -1 << -1                          // move row
      << -1 << -1                          // move col
      << 40 << 40                          // cell size
      << 20 << 20 << 80 << 80              // rect
      << QItemSelectionModel::Select       // command
      << 9;                                // selected count

    QTest::newRow("no span, col hidden, no moved, 3x3 select")
      << 10 << 10                          // dim
      << -1 << -1                          // pos
      << 1 << 1                            // span
      << -1 << 1                           // hide
      << -1 << -1                          // move row
      << -1 << -1                          // move col
      << 40 << 40                          // cell size
      << 20 << 20 << 80 << 80              // rect
      << QItemSelectionModel::Select       // command
      << 9;                                // selected count

    QTest::newRow("no span, no hidden, row moved, 3x3 select")
      << 10 << 10                          // dim
      << -1 << -1                          // pos
      << 1 << 1                            // span
      << -1 << -1                          // hide
      << 1 << 3                            // move row
      << -1 << -1                          // move col
      << 40 << 40                          // cell size
      << 20 << 20 << 80 << 80              // rect
      << QItemSelectionModel::Select       // command
      << 9;                                // selected count

    QTest::newRow("no span, no hidden, col moved, 3x3 select")
      << 10 << 10                          // dim
      << -1 << -1                          // pos
      << 1 << 1                            // span
      << -1 << -1                          // hide
      << -1 << -1                          // move row
      << 1 << 3                            // move col
      << 40 << 40                          // cell size
      << 20 << 20 << 80 << 80              // rect
      << QItemSelectionModel::Select       // command
      << 9;                                // selected count
}

void tst_QTableView::selection()
{
    QFETCH(int, rowCount);
    QFETCH(int, columnCount);
    QFETCH(int, row);
    QFETCH(int, column);
    QFETCH(int, rowSpan);
    QFETCH(int, columnSpan);
    QFETCH(int, hideRow);
    QFETCH(int, hideColumn);
    QFETCH(int, moveRowFrom);
    QFETCH(int, moveRowTo);
    QFETCH(int, moveColumnFrom);
    QFETCH(int, moveColumnTo);
    QFETCH(int, rowHeight);
    QFETCH(int, columnWidth);
    QFETCH(int, x);
    QFETCH(int, y);
    QFETCH(int, width);
    QFETCH(int, height);
    QFETCH(QItemSelectionModel::SelectionFlag, command);
    QFETCH(int, selectedCount);

    QtTestTableModel model(rowCount, columnCount);

    QtTestTableView view;
    view.show();
    view.setModel(&model);

    view.setSpan(row, column, rowSpan, columnSpan);

    view.hideRow(hideRow);
    view.hideColumn(hideColumn);

    view.verticalHeader()->moveSection(moveRowFrom, moveRowTo);
    view.horizontalHeader()->moveSection(moveColumnFrom, moveColumnTo);

    for (int r = 0; r < rowCount; ++r)
        view.setRowHeight(r, rowHeight);
    for (int c = 0; c < columnCount; ++c)
        view.setColumnWidth(c, columnWidth);

    view.setSelection(QRect(x, y, width, height), command);

    QCOMPARE(view.selectedIndexes().size(), selectedCount);
}

void tst_QTableView::selectRow_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("columnCount");
    QTest::addColumn<int>("row");
    QTest::addColumn<QAbstractItemView::SelectionMode>("mode");
    QTest::addColumn<QAbstractItemView::SelectionBehavior>("behavior");
    QTest::addColumn<int>("selectedItems");

    QTest::newRow("SingleSelection and SelectItems")
        << 10 << 10
        << 0
        << QAbstractItemView::SingleSelection
        << QAbstractItemView::SelectItems
        << 0;

    QTest::newRow("SingleSelection and SelectRows")
        << 10 << 10
        << 0
        << QAbstractItemView::SingleSelection
        << QAbstractItemView::SelectRows
        << 10;

    QTest::newRow("SingleSelection and SelectColumns")
        << 10 << 10
        << 0
        << QAbstractItemView::SingleSelection
        << QAbstractItemView::SelectColumns
        << 0;

    QTest::newRow("MultiSelection and SelectItems")
        << 10 << 10
        << 0
        << QAbstractItemView::MultiSelection
        << QAbstractItemView::SelectItems
        << 10;

    QTest::newRow("MultiSelection and SelectRows")
        << 10 << 10
        << 0
        << QAbstractItemView::MultiSelection
        << QAbstractItemView::SelectRows
        << 10;

    QTest::newRow("MultiSelection and SelectColumns")
        << 10 << 10
        << 0
        << QAbstractItemView::MultiSelection
        << QAbstractItemView::SelectColumns
        << 0;

    QTest::newRow("ExtendedSelection and SelectItems")
        << 10 << 10
        << 0
        << QAbstractItemView::ExtendedSelection
        << QAbstractItemView::SelectItems
        << 10;

    QTest::newRow("ExtendedSelection and SelectRows")
        << 10 << 10
        << 0
        << QAbstractItemView::ExtendedSelection
        << QAbstractItemView::SelectRows
        << 10;

    QTest::newRow("ExtendedSelection and SelectColumns")
        << 10 << 10
        << 0
        << QAbstractItemView::ExtendedSelection
        << QAbstractItemView::SelectColumns
        << 0;

    QTest::newRow("ContiguousSelection and SelectItems")
        << 10 << 10
        << 0
        << QAbstractItemView::ContiguousSelection
        << QAbstractItemView::SelectItems
        << 10;

    QTest::newRow("ContiguousSelection and SelectRows")
        << 10 << 10
        << 0
        << QAbstractItemView::ContiguousSelection
        << QAbstractItemView::SelectRows
        << 10;

    QTest::newRow("ContiguousSelection and SelectColumns")
        << 10 << 10
        << 0
        << QAbstractItemView::ContiguousSelection
        << QAbstractItemView::SelectColumns
        << 0;
}

void tst_QTableView::selectRow()
{
    QFETCH(int, rowCount);
    QFETCH(int, columnCount);
    QFETCH(int, row);
    QFETCH(QAbstractItemView::SelectionMode, mode);
    QFETCH(QAbstractItemView::SelectionBehavior, behavior);
    QFETCH(int, selectedItems);

    QtTestTableModel model(rowCount, columnCount);
    QTableView view;

    view.setModel(&model);
    view.setSelectionMode(mode);
    view.setSelectionBehavior(behavior);

    QCOMPARE(view.selectionModel()->selectedIndexes().size(), 0);

    view.selectRow(row);

    //test we have 10 items selected
    QCOMPARE(view.selectionModel()->selectedIndexes().size(), selectedItems);
    //test that all 10 items are in the same row
    for (int i = 0; selectedItems > 0 && i < rowCount; ++i)
        QCOMPARE(view.selectionModel()->selectedIndexes().at(i).row(), row);
}

void tst_QTableView::selectColumn_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("columnCount");
    QTest::addColumn<int>("column");
    QTest::addColumn<QAbstractItemView::SelectionMode>("mode");
    QTest::addColumn<QAbstractItemView::SelectionBehavior>("behavior");
    QTest::addColumn<int>("selectedItems");

        QTest::newRow("SingleSelection and SelectItems")
            << 10 << 10
            << 0
            << QAbstractItemView::SingleSelection
            << QAbstractItemView::SelectItems
            << 0;

        QTest::newRow("SingleSelection and SelectRows")
            << 10 << 10
            << 0
            << QAbstractItemView::SingleSelection
            << QAbstractItemView::SelectRows
            << 0;

        QTest::newRow("SingleSelection and SelectColumns")
            << 10 << 10
            << 0
            << QAbstractItemView::SingleSelection
            << QAbstractItemView::SelectColumns
            << 10;

        QTest::newRow("MultiSelection and SelectItems")
            << 10 << 10
            << 0
            << QAbstractItemView::MultiSelection
            << QAbstractItemView::SelectItems
            << 10;

        QTest::newRow("MultiSelection and SelectRows")
            << 10 << 10
            << 0
            << QAbstractItemView::MultiSelection
            << QAbstractItemView::SelectRows
            << 0;

        QTest::newRow("MultiSelection and SelectColumns")
            << 10 << 10
            << 0
            << QAbstractItemView::MultiSelection
            << QAbstractItemView::SelectColumns
            << 10;

        QTest::newRow("ExtendedSelection and SelectItems")
            << 10 << 10
            << 0
            << QAbstractItemView::ExtendedSelection
            << QAbstractItemView::SelectItems
            << 10;

        QTest::newRow("ExtendedSelection and SelectRows")
            << 10 << 10
            << 0
            << QAbstractItemView::ExtendedSelection
            << QAbstractItemView::SelectRows
            << 0;

        QTest::newRow("ExtendedSelection and SelectColumns")
            << 10 << 10
            << 0
            << QAbstractItemView::ExtendedSelection
            << QAbstractItemView::SelectColumns
            << 10;

        QTest::newRow("ContiguousSelection and SelectItems")
            << 10 << 10
            << 0
            << QAbstractItemView::ContiguousSelection
            << QAbstractItemView::SelectItems
            << 10;

        QTest::newRow("ContiguousSelection and SelectRows")
            << 10 << 10
            << 0
            << QAbstractItemView::ContiguousSelection
            << QAbstractItemView::SelectRows
            << 0;

        QTest::newRow("ContiguousSelection and SelectColumns")
            << 10 << 10
            << 0
            << QAbstractItemView::ContiguousSelection
            << QAbstractItemView::SelectColumns
            << 10;
}

void tst_QTableView::selectColumn()
{
    QFETCH(int, rowCount);
    QFETCH(int, columnCount);
    QFETCH(int, column);
    QFETCH(QAbstractItemView::SelectionMode, mode);
    QFETCH(QAbstractItemView::SelectionBehavior, behavior);
    QFETCH(int, selectedItems);

    QtTestTableModel model(rowCount, columnCount);

    QTableView view;
    view.setModel(&model);
    view.setSelectionMode(mode);
    view.setSelectionBehavior(behavior);

    QCOMPARE(view.selectionModel()->selectedIndexes().size(), 0);

    view.selectColumn(column);

    QCOMPARE(view.selectionModel()->selectedIndexes().size(), selectedItems);
    for (int i = 0; selectedItems > 0 && i < columnCount; ++i)
        QCOMPARE(view.selectionModel()->selectedIndexes().at(i).column(), column);
}

#if QT_CONFIG(shortcut)

void tst_QTableView::selectall_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("columnCount");
    QTest::addColumn<int>("row");
    QTest::addColumn<int>("column");
    QTest::addColumn<int>("rowSpan");
    QTest::addColumn<int>("columnSpan");
    QTest::addColumn<int>("hideRow");
    QTest::addColumn<int>("hideColumn");
    QTest::addColumn<int>("moveRowFrom");
    QTest::addColumn<int>("moveRowTo");
    QTest::addColumn<int>("moveColumnFrom");
    QTest::addColumn<int>("moveColumnTo");
    QTest::addColumn<int>("rowHeight");
    QTest::addColumn<int>("columnWidth");
    QTest::addColumn<int>("selectedCount"); // ### make this more detailed

    QTest::newRow("no span, no hidden, no moved")
      << 10 << 10                          // dim
      << -1 << -1                          // pos
      << 1 << 1                            // span
      << -1 << -1                          // hide
      << -1 << -1                          // move row
      << -1 << -1                          // move col
      << 40 << 40                          // cell size
      << 100;                              // selected count

    QTest::newRow("row span, no hidden, no moved")
      << 10 << 10                          // dim
      << 1 << 1                            // pos
      << 2 << 1                            // span
      << -1 << -1                          // hide
      << -1 << -1                          // move row
      << -1 << -1                          // move col
      << 40 << 40                          // cell size
      << 99;                               // selected count

    QTest::newRow("col span, no hidden, no moved")
      << 10 << 10                          // dim
      << 1 << 1                            // pos
      << 1 << 2                            // span
      << -1 << -1                          // hide
      << -1 << -1                          // move row
      << -1 << -1                          // move col
      << 40 << 40                          // cell size
      << 99;                               // selected count

    QTest::newRow("no span, row hidden, no moved")
      << 10 << 10                          // dim
      << -1 << -1                          // pos
      << 1 << 1                            // span
      << 1 << -1                           // hide
      << -1 << -1                          // move row
      << -1 << -1                          // move col
      << 40 << 40                          // cell size
      << 90;                               // selected count

    QTest::newRow("no span, col hidden, no moved")
      << 10 << 10                          // dim
      << -1 << -1                          // pos
      << 1 << 1                            // span
      << -1 << 1                           // hide
      << -1 << -1                          // move row
      << -1 << -1                          // move col
      << 40 << 40                          // cell size
      << 90;                               // selected count

    QTest::newRow("no span, no hidden, row moved")
      << 10 << 10                          // dim
      << -1 << -1                          // pos
      << 1 << 1                            // span
      << -1 << -1                          // hide
      << 1 << 3                            // move row
      << -1 << -1                          // move col
      << 40 << 40                          // cell size
      << 100;                              // selected count

    QTest::newRow("no span, no hidden, col moved")
      << 10 << 10                          // dim
      << -1 << -1                          // pos
      << 1 << 1                            // span
      << -1 << -1                          // hide
      << -1 << -1                          // move row
      << 1 << 3                            // move col
      << 40 << 40                          // cell size
      << 100;                              // selected count
}

void QTest__keySequence(QWidget* widget, const QKeySequence &ks)
{
    for (int i = 0; i < ks.count(); ++i)
    {
        Qt::Key key = ks[i].key();
        Qt::KeyboardModifiers modifiers = ks[i].keyboardModifiers();
        QTest::keyClick(widget, key, modifiers);
    }
}

void tst_QTableView::selectall()
{
    QFETCH(int, rowCount);
    QFETCH(int, columnCount);
    QFETCH(int, row);
    QFETCH(int, column);
    QFETCH(int, rowSpan);
    QFETCH(int, columnSpan);
    QFETCH(int, hideRow);
    QFETCH(int, hideColumn);
    QFETCH(int, moveRowFrom);
    QFETCH(int, moveRowTo);
    QFETCH(int, moveColumnFrom);
    QFETCH(int, moveColumnTo);
    QFETCH(int, rowHeight);
    QFETCH(int, columnWidth);
    QFETCH(int, selectedCount);

    QtTestTableModel model(rowCount, columnCount);

    QtTestTableView view;
    view.show();
    view.setModel(&model);

    view.setSpan(row, column, rowSpan, columnSpan);

    view.hideRow(hideRow);
    view.hideColumn(hideColumn);

    view.verticalHeader()->moveSection(moveRowFrom, moveRowTo);
    view.horizontalHeader()->moveSection(moveColumnFrom, moveColumnTo);

    for (int r = 0; r < rowCount; ++r)
        view.setRowHeight(r, rowHeight);
    for (int c = 0; c < columnCount; ++c)
        view.setColumnWidth(c, columnWidth);

    // try slot first
    view.clearSelection();
    QCOMPARE(view.selectedIndexes().size(), 0);
    view.selectAll();
    QCOMPARE(view.selectedIndexes().size(), selectedCount);

    // try by key sequence
    view.clearSelection();
    QCOMPARE(view.selectedIndexes().size(), 0);
    QTest__keySequence(&view, QKeySequence(QKeySequence::SelectAll));
    QCOMPARE(view.selectedIndexes().size(), selectedCount);

    // check again with no selection mode
    view.clearSelection();
    view.setSelectionMode(QAbstractItemView::NoSelection);
    QCOMPARE(view.selectedIndexes().size(), 0);
    QTest__keySequence(&view, QKeySequence(QKeySequence::SelectAll));
    QCOMPARE(view.selectedIndexes().size(), 0);
}

#endif // QT_CONFIG(shortcut)

void tst_QTableView::visualRect_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("columnCount");
    QTest::addColumn<int>("hideRow");
    QTest::addColumn<int>("hideColumn");
    QTest::addColumn<int>("row");
    QTest::addColumn<int>("column");
    QTest::addColumn<int>("rowHeight");
    QTest::addColumn<int>("columnWidth");
    QTest::addColumn<QRect>("expectedRect");

    QTest::newRow("(0,0)")
        << 10 << 10
        << -1 << -1
        << 0 << 0
        << 20 << 30
        << QRect(0, 0, 29, 19);

    QTest::newRow("(0,0) hidden row")
        << 10 << 10
        << 0 << -1
        << 0 << 0
        << 20 << 30
        << QRect();

    QTest::newRow("(0,0) hidden column")
        << 10 << 10
        << -1 << 0
        << 0 << 0
        << 20 << 30
        << QRect();

    QTest::newRow("(0,0) hidden row and column")
        << 10 << 10
        << 0 << 0
        << 0 << 0
        << 20 << 30
        << QRect();

    QTest::newRow("(0,0) out of bounds")
        << 10 << 10
        << -1 << -1
        << 20 << 20
        << 20 << 30
        << QRect();

    QTest::newRow("(5,5), hidden row")
        << 10 << 10
        << 5 << -1
        << 5 << 5
        << 20 << 30
        << QRect();

    QTest::newRow("(9,9)")
        << 10 << 10
        << -1 << -1
        << 9 << 9
        << 20 << 30
        << QRect(30*9, 20*9, 29, 19);
}

void tst_QTableView::visualRect()
{
    QFETCH(int, rowCount);
    QFETCH(int, columnCount);
    QFETCH(int, hideRow);
    QFETCH(int, hideColumn);
    QFETCH(int, row);
    QFETCH(int, column);
    QFETCH(int, rowHeight);
    QFETCH(int, columnWidth);
    QFETCH(QRect, expectedRect);

    QtTestTableModel model(rowCount, columnCount);

    QTableView view;
    view.setModel(&model);
    view.horizontalHeader()->setMinimumSectionSize(0);
    view.verticalHeader()->setMinimumSectionSize(0);
    // Make sure that it has 1 pixel between each cell.
    view.setGridStyle(Qt::SolidLine);
    for (int i = 0; i < view.verticalHeader()->count(); ++i)
        view.verticalHeader()->resizeSection(i, rowHeight);
    for (int i = 0; i < view.horizontalHeader()->count(); ++i)
        view.horizontalHeader()->resizeSection(i, columnWidth);

    view.hideRow(hideRow);
    view.hideColumn(hideColumn);

    QRect rect = view.visualRect(model.index(row, column));
    QCOMPARE(rect, expectedRect);
}

void tst_QTableView::fetchMore()
{
    QtTestTableModel model(64, 64);

    model.can_fetch_more = true;

    QTableView view;
    view.setModel(&model);
    view.show();

    QCOMPARE(model.fetch_more_count, 0);
    view.verticalScrollBar()->setValue(view.verticalScrollBar()->maximum());
    QVERIFY(model.fetch_more_count > 0);

    model.fetch_more_count = 0; //reset
    view.scrollToTop();
    QCOMPARE(model.fetch_more_count, 0);

    view.scrollToBottom();
    QVERIFY(model.fetch_more_count > 0);

    model.fetch_more_count = 0; //reset
    view.scrollToTop();
    view.setCurrentIndex(model.index(0, 0));
    QCOMPARE(model.fetch_more_count, 0);

    for (int i = 0; i < 64; ++i)
        QTest::keyClick(&view, Qt::Key_Down);
    QCOMPARE(view.currentIndex(), model.index(63, 0));
    QVERIFY(model.fetch_more_count > 0);
}

void tst_QTableView::setHeaders()
{
    QTableView view;

    // Make sure we don't delete ourselves
    view.setVerticalHeader(view.verticalHeader());
    view.verticalHeader()->count();
    view.setHorizontalHeader(view.horizontalHeader());
    view.horizontalHeader()->count();

    // Try passing around a header without it being deleted
    QTableView view2;
    view2.setVerticalHeader(view.verticalHeader());
    view2.setHorizontalHeader(view.horizontalHeader());
    view.setHorizontalHeader(new QHeaderView(Qt::Horizontal));
    view.setVerticalHeader(new QHeaderView(Qt::Vertical));
    view2.verticalHeader()->count();
    view2.horizontalHeader()->count();

}

void tst_QTableView::resizeRowsToContents_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("columnCount");
    QTest::addColumn<bool>("showGrid");
    QTest::addColumn<int>("cellWidth");
    QTest::addColumn<int>("cellHeight");
    QTest::addColumn<int>("rowHeight");
    QTest::addColumn<int>("columnWidth");

    QTest::newRow("10x10 grid shown 40x40")
        << 10 << 10 << false << 40 << 40 << 40 << 40;

    QTest::newRow("10x10 grid not shown 40x40")
        << 10 << 10 << true << 40 << 40 << 41 << 41;
}

void tst_QTableView::resizeRowsToContents()
{
    QFETCH(int, rowCount);
    QFETCH(int, columnCount);
    QFETCH(bool, showGrid);
    QFETCH(int, cellWidth);
    QFETCH(int, cellHeight);
    QFETCH(int, rowHeight);
    QFETCH(int, columnWidth);
    Q_UNUSED(columnWidth);

    QtTestTableModel model(rowCount, columnCount);
    QtTestTableView view;
    QtTestItemDelegate delegate;

    view.setModel(&model);
    view.setItemDelegate(&delegate);
    view.setShowGrid(showGrid); // the grid will add to the row height

    delegate.hint = QSize(cellWidth, cellHeight);

    QSignalSpy resizedSpy(view.verticalHeader(), &QHeaderView::sectionResized);
    view.resizeRowsToContents();

    QCOMPARE(resizedSpy.size(), model.rowCount());
    for (int r = 0; r < model.rowCount(); ++r)
        QCOMPARE(view.rowHeight(r), rowHeight);
}

void tst_QTableView::resizeColumnsToContents_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("columnCount");
    QTest::addColumn<bool>("showGrid");
    QTest::addColumn<int>("cellWidth");
    QTest::addColumn<int>("cellHeight");
    QTest::addColumn<int>("rowHeight");
    QTest::addColumn<int>("columnWidth");

    QTest::newRow("10x10 grid not shown 60x60")
        << 10 << 10 << false << 60 << 60 << 60 << 60;

    QTest::newRow("10x10 grid shown 60x60")
        << 10 << 10 << true << 60 << 60 << 61 << 61;
}

void tst_QTableView::resizeColumnsToContents()
{
    QFETCH(int, rowCount);
    QFETCH(int, columnCount);
    QFETCH(bool, showGrid);
    QFETCH(int, cellWidth);
    QFETCH(int, cellHeight);
    QFETCH(int, rowHeight);
    QFETCH(int, columnWidth);
    Q_UNUSED(rowHeight);

    QtTestTableModel model(rowCount, columnCount);
    QtTestTableView view;
    QtTestItemDelegate delegate;

    view.setModel(&model);
    view.setItemDelegate(&delegate);
    view.setShowGrid(showGrid); // the grid will add to the row height

    delegate.hint = QSize(cellWidth, cellHeight);

    QSignalSpy resizedSpy(view.horizontalHeader(), &QHeaderView::sectionResized);
    view.resizeColumnsToContents();

    QCOMPARE(resizedSpy.size(), model.columnCount());
    for (int c = 0; c < model.columnCount(); ++c)
        QCOMPARE(view.columnWidth(c), columnWidth);
}

void tst_QTableView::rowViewportPosition_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("rowHeight");
    QTest::addColumn<int>("row");
    QTest::addColumn<QAbstractItemView::ScrollMode>("verticalScrollMode");
    QTest::addColumn<int>("verticalScrollValue");
    QTest::addColumn<int>("rowViewportPosition");

    QTest::newRow("row 0, scroll per item 0")
        << 100 << 40 << 0 << QAbstractItemView::ScrollPerItem << 0 << 0;

    QTest::newRow("row 1, scroll per item, 0")
        << 100 << 40 << 1 << QAbstractItemView::ScrollPerItem << 0 << 1 * 40;

    QTest::newRow("row 1, scroll per item, 1")
        << 100 << 40 << 1 << QAbstractItemView::ScrollPerItem << 1 << 0;

    QTest::newRow("row 5, scroll per item, 0")
        << 100 << 40 << 5 << QAbstractItemView::ScrollPerItem << 0 << 5 * 40;

    QTest::newRow("row 5, scroll per item, 5")
        << 100 << 40 << 5 << QAbstractItemView::ScrollPerItem << 5 << 0;

    QTest::newRow("row 9, scroll per item, 0")
        << 100 << 40 << 9 << QAbstractItemView::ScrollPerItem << 0 << 9 * 40;

    QTest::newRow("row 9, scroll per item, 5")
        << 100 << 40 << 9 << QAbstractItemView::ScrollPerItem << 5 << 4 * 40;

    QTest::newRow("row 0, scroll per pixel 0")
        << 100 << 40 << 0 << QAbstractItemView::ScrollPerPixel << 0 << 0;

    QTest::newRow("row 1, scroll per pixel, 0")
        << 100 << 40 << 1 << QAbstractItemView::ScrollPerPixel << 0 << 1 * 40;

    QTest::newRow("row 1, scroll per pixel, 1")
        << 100 << 40 << 1 << QAbstractItemView::ScrollPerPixel << 1 * 40 << 0;

    QTest::newRow("row 5, scroll per pixel, 0")
        << 100 << 40 << 5 << QAbstractItemView::ScrollPerPixel << 0 << 5 * 40;

    QTest::newRow("row 5, scroll per pixel, 5")
        << 100 << 40 << 5 << QAbstractItemView::ScrollPerPixel << 5 * 40 << 0;

    QTest::newRow("row 9, scroll per pixel, 0")
        << 100 << 40 << 9 << QAbstractItemView::ScrollPerPixel << 0 << 9 * 40;

    QTest::newRow("row 9, scroll per pixel, 5")
        << 100 << 40 << 9 << QAbstractItemView::ScrollPerPixel << 5 * 40 << 4 * 40;
}

void tst_QTableView::rowViewportPosition()
{
    QFETCH(int, rowCount);
    QFETCH(int, rowHeight);
    QFETCH(int, row);
    QFETCH(QAbstractItemView::ScrollMode, verticalScrollMode);
    QFETCH(int, verticalScrollValue);
    QFETCH(int, rowViewportPosition);

    QtTestTableModel model(rowCount, 1);
    QtTestTableView view;
    setFrameless(&view);
    view.resize(100, 2 * rowHeight);
    view.show();

    view.setModel(&model);
    for (int r = 0; r < rowCount; ++r)
        view.setRowHeight(r, rowHeight);

    view.setVerticalScrollMode(verticalScrollMode);
    view.verticalScrollBar()->setValue(verticalScrollValue);

    QCOMPARE(view.rowViewportPosition(row), rowViewportPosition);
}

void tst_QTableView::rowAt_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("rowHeight");
    QTest::addColumn<IntList>("hiddenRows");
    QTest::addColumn<int>("coordinate");
    QTest::addColumn<int>("row");

    QTest::newRow("row at 100") << 5 << 40 << IntList() << 100 << 2;
    QTest::newRow("row at 180") << 5 << 40 << IntList() << 180 << 4;
    QTest::newRow("row at 20")  << 5 << 40 << IntList() <<  20 << 0;

    // ### expand the dataset to include hidden rows
}

void tst_QTableView::rowAt()
{
    QFETCH(int, rowCount);
    QFETCH(int, rowHeight);
    QFETCH(IntList, hiddenRows);
    QFETCH(int, coordinate);
    QFETCH(int, row);

    QtTestTableModel model(rowCount, 1);
    QtTestTableView view;
    view.resize(100, 2 * rowHeight);

    view.setModel(&model);

    for (int r = 0; r < rowCount; ++r)
        view.setRowHeight(r, rowHeight);

    for (int i = 0; i < hiddenRows.size(); ++i)
        view.hideRow(hiddenRows.at(i));

    QCOMPARE(view.rowAt(coordinate), row);
}

void tst_QTableView::rowHeight_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<IntList>("rowHeights");
    QTest::addColumn<BoolList>("hiddenRows");

    QTest::newRow("increasing")
      << 5
      << (IntList() << 20 << 30 << 40 << 50 << 60)
      << (BoolList() << false << false << false << false << false);

    QTest::newRow("decreasing")
      << 5
      << (IntList() << 60 << 50 << 40 << 30 << 20)
      << (BoolList() << false << false << false << false << false);

    QTest::newRow("random")
      << 5
      << (IntList() << 87 << 34 << 68 << 91 << 27)
      << (BoolList() << false << false << false << false << false);

    // ### expand the dataset to include hidden rows
}

void tst_QTableView::rowHeight()
{
    QFETCH(int, rowCount);
    QFETCH(IntList, rowHeights);
    QFETCH(BoolList, hiddenRows);

    QtTestTableModel model(rowCount, 1);
    QtTestTableView view;

    view.setModel(&model);

    for (int r = 0; r < rowCount; ++r) {
        view.setRowHeight(r, rowHeights.at(r));
        view.setRowHidden(r, hiddenRows.at(r));
    }

    for (int r = 0; r < rowCount; ++r) {
        if (hiddenRows.at(r))
            QCOMPARE(view.rowHeight(r), 0);
        else
            QCOMPARE(view.rowHeight(r), rowHeights.at(r));
    }
}

void tst_QTableView::columnViewportPosition_data()
{
    QTest::addColumn<int>("columnCount");
    QTest::addColumn<int>("columnWidth");
    QTest::addColumn<int>("column");
    QTest::addColumn<QAbstractItemView::ScrollMode>("horizontalScrollMode");
    QTest::addColumn<int>("horizontalScrollValue");
    QTest::addColumn<int>("columnViewportPosition");

    QTest::newRow("column 0, scroll per item 0")
        << 100 << 40 << 0 << QAbstractItemView::ScrollPerItem << 0 << 0;

    QTest::newRow("column 1, scroll per item, 0")
        << 100 << 40 << 1 << QAbstractItemView::ScrollPerItem << 0 << 1 * 40;

    QTest::newRow("column 1, scroll per item, 1")
        << 100 << 40 << 1 << QAbstractItemView::ScrollPerItem << 1 << 0;

    QTest::newRow("column 5, scroll per item, 0")
        << 100 << 40 << 5 << QAbstractItemView::ScrollPerItem << 0 << 5 * 40;

    QTest::newRow("column 5, scroll per item, 5")
        << 100 << 40 << 5 << QAbstractItemView::ScrollPerItem << 5 << 0;

    QTest::newRow("column 9, scroll per item, 0")
        << 100 << 40 << 9 << QAbstractItemView::ScrollPerItem << 0 << 9 * 40;

    QTest::newRow("column 9, scroll per item, 5")
        << 100 << 40 << 9 << QAbstractItemView::ScrollPerItem << 5 << 4 * 40;

    QTest::newRow("column 0, scroll per pixel 0")
        << 100 << 40 << 0 << QAbstractItemView::ScrollPerPixel << 0 << 0;

    QTest::newRow("column 1, scroll per pixel 0")
        << 100 << 40 << 1 << QAbstractItemView::ScrollPerPixel << 0 << 1 * 40;

    QTest::newRow("column 1, scroll per pixel 1")
        << 100 << 40 << 1 << QAbstractItemView::ScrollPerPixel << 1 * 40 << 0;

    QTest::newRow("column 5, scroll per pixel 0")
        << 100 << 40 << 5 << QAbstractItemView::ScrollPerPixel << 0 << 5 * 40;

    QTest::newRow("column 5, scroll per pixel 5")
        << 100 << 40 << 5 << QAbstractItemView::ScrollPerPixel << 5 * 40 << 0;

    QTest::newRow("column 9, scroll per pixel 0")
        << 100 << 40 << 9 << QAbstractItemView::ScrollPerPixel << 0 << 9 * 40;

    QTest::newRow("column 9, scroll per pixel 5")
        << 100 << 40 << 9 << QAbstractItemView::ScrollPerPixel << 5 * 40 << 4 * 40;
}

void tst_QTableView::columnViewportPosition()
{
    QFETCH(int, columnCount);
    QFETCH(int, columnWidth);
    QFETCH(int, column);
    QFETCH(QAbstractItemView::ScrollMode, horizontalScrollMode);
    QFETCH(int, horizontalScrollValue);
    QFETCH(int, columnViewportPosition);

    QtTestTableModel model(1, columnCount);
    QtTestTableView view;
    setFrameless(&view);
    view.resize(2 * columnWidth, 100);
    view.show();

    view.setModel(&model);
    for (int c = 0; c < columnCount; ++c)
        view.setColumnWidth(c, columnWidth);

    view.setHorizontalScrollMode(horizontalScrollMode);
    view.horizontalScrollBar()->setValue(horizontalScrollValue);

    QCOMPARE(view.columnViewportPosition(column), columnViewportPosition);
}

void tst_QTableView::columnAt_data()
{
    QTest::addColumn<int>("columnCount");
    QTest::addColumn<int>("columnWidth");
    QTest::addColumn<IntList>("hiddenColumns");
    QTest::addColumn<int>("coordinate");
    QTest::addColumn<int>("column");

    QTest::newRow("column at 100") << 5 << 40 << IntList() << 100 << 2;
    QTest::newRow("column at 180") << 5 << 40 << IntList() << 180 << 4;
    QTest::newRow("column at 20")  << 5 << 40 << IntList() <<  20 << 0;

    // ### expand the dataset to include hidden coumns
}

void tst_QTableView::columnAt()
{
    QFETCH(int, columnCount);
    QFETCH(int, columnWidth);
    QFETCH(IntList, hiddenColumns);
    QFETCH(int, coordinate);
    QFETCH(int, column);

    QtTestTableModel model(1, columnCount);
    QtTestTableView view;
    view.resize(2 * columnWidth, 100);

    view.setModel(&model);

    for (int c = 0; c < columnCount; ++c)
        view.setColumnWidth(c, columnWidth);

    for (int i = 0; i < hiddenColumns.size(); ++i)
        view.hideColumn(hiddenColumns.at(i));

    QCOMPARE(view.columnAt(coordinate), column);
}

void tst_QTableView::columnWidth_data()
{
    QTest::addColumn<int>("columnCount");
    QTest::addColumn<IntList>("columnWidths");
    QTest::addColumn<BoolList>("hiddenColumns");

    QTest::newRow("increasing")
      << 5
      << (IntList() << 20 << 30 << 40 << 50 << 60)
      << (BoolList() << false << false << false << false << false);

    QTest::newRow("decreasing")
      << 5
      << (IntList() << 60 << 50 << 40 << 30 << 20)
      << (BoolList() << false << false << false << false << false);

    QTest::newRow("random")
      << 5
      << (IntList() << 87 << 34 << 68 << 91 << 27)
      << (BoolList() << false << false << false << false << false);

    // ### expand the dataset to include hidden columns
}

void tst_QTableView::columnWidth()
{
    QFETCH(int, columnCount);
    QFETCH(IntList, columnWidths);
    QFETCH(BoolList, hiddenColumns);

    QtTestTableModel model(1, columnCount);
    QtTestTableView view;

    view.setModel(&model);

    for (int c = 0; c < columnCount; ++c) {
        view.setColumnWidth(c, columnWidths.at(c));
        view.setColumnHidden(c, hiddenColumns.at(c));
    }

    for (int c = 0; c < columnCount; ++c) {
        if (hiddenColumns.at(c))
            QCOMPARE(view.columnWidth(c), 0);
        else
            QCOMPARE(view.columnWidth(c), columnWidths.at(c));
    }
}

void tst_QTableView::hiddenRow_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<BoolList>("hiddenRows");

    QTest::newRow("first hidden")
      << 5 << (BoolList() << true << false << false << false << false);

    QTest::newRow("last hidden")
      << 5 << (BoolList() << false << false << false << false << true);

    QTest::newRow("none hidden")
      << 5 << (BoolList() << false << false << false << false << false);

    QTest::newRow("all hidden")
      << 5 << (BoolList() << true << true << true << true << true);
 }

void tst_QTableView::hiddenRow()
{
    QFETCH(int, rowCount);
    QFETCH(BoolList, hiddenRows);


    QtTestTableModel model(rowCount, 1);
    QtTestTableView view;

    view.setModel(&model);

    for (int r = 0; r < rowCount; ++r)
        QVERIFY(!view.isRowHidden(r));

    for (int r = 0; r < rowCount; ++r)
        view.setRowHidden(r, hiddenRows.at(r));

    for (int r = 0; r < rowCount; ++r)
        QCOMPARE(view.isRowHidden(r), hiddenRows.at(r));

    for (int r = 0; r < rowCount; ++r)
        view.setRowHidden(r, false);

    for (int r = 0; r < rowCount; ++r)
        QVERIFY(!view.isRowHidden(r));
}

void tst_QTableView::hiddenColumn_data()
{
    QTest::addColumn<int>("columnCount");
    QTest::addColumn<BoolList>("hiddenColumns");

    QTest::newRow("first hidden")
      << 5 << (BoolList() << true << false << false << false << false);

    QTest::newRow("last hidden")
      << 5 << (BoolList() << false << false << false << false << true);

    QTest::newRow("none hidden")
      << 5 << (BoolList() << false << false << false << false << false);

    QTest::newRow("all hidden")
      << 5 << (BoolList() << true << true << true << true << true);
}

void tst_QTableView::hiddenColumn()
{
    QFETCH(int, columnCount);
    QFETCH(BoolList, hiddenColumns);

    QtTestTableModel model(1, columnCount);
    QtTestTableView view;

    view.setModel(&model);

    for (int c = 0; c < columnCount; ++c)
        QVERIFY(!view.isColumnHidden(c));

    for (int c = 0; c < columnCount; ++c)
        view.setColumnHidden(c, hiddenColumns.at(c));

    for (int c = 0; c < columnCount; ++c)
        QCOMPARE(view.isColumnHidden(c), hiddenColumns.at(c));

    for (int c = 0; c < columnCount; ++c)
        view.setColumnHidden(c, false);

    for (int c = 0; c < columnCount; ++c)
        QVERIFY(!view.isColumnHidden(c));
}

void tst_QTableView::sortingEnabled_data()
{
//    QTest::addColumn<int>("columnCount");
}

void tst_QTableView::sortingEnabled()
{
//    QFETCH(int, columnCount);
}

void tst_QTableView::sortByColumn_data()
{
    QTest::addColumn<bool>("sortingEnabled");
    QTest::newRow("sorting enabled") << true;
    QTest::newRow("sorting disabled") << false;
}

// Checks sorting and that sortByColumn also sets the sortIndicator
void tst_QTableView::sortByColumn()
{
    QFETCH(bool, sortingEnabled);
    QTableView view;
    QStandardItemModel model(4, 2);
    QSortFilterProxyModel sfpm; // default QStandardItemModel does not support 'unsorted' state
    sfpm.setSourceModel(&model);
    model.setItem(0, 0, new QStandardItem("b"));
    model.setItem(1, 0, new QStandardItem("d"));
    model.setItem(2, 0, new QStandardItem("c"));
    model.setItem(3, 0, new QStandardItem("a"));
    model.setItem(0, 1, new QStandardItem("e"));
    model.setItem(1, 1, new QStandardItem("g"));
    model.setItem(2, 1, new QStandardItem("h"));
    model.setItem(3, 1, new QStandardItem("f"));

    view.setSortingEnabled(sortingEnabled);
    view.setModel(&sfpm);
    view.show();

    view.sortByColumn(1, Qt::DescendingOrder);
    QCOMPARE(view.horizontalHeader()->sortIndicatorSection(), 1);
    QCOMPARE(view.model()->data(view.model()->index(0, 0)).toString(), QString::fromLatin1("c"));
    QCOMPARE(view.model()->data(view.model()->index(1, 0)).toString(), QString::fromLatin1("d"));
    QCOMPARE(view.model()->data(view.model()->index(0, 1)).toString(), QString::fromLatin1("h"));
    QCOMPARE(view.model()->data(view.model()->index(1, 1)).toString(), QString::fromLatin1("g"));

    view.sortByColumn(0, Qt::AscendingOrder);
    QCOMPARE(view.horizontalHeader()->sortIndicatorSection(), 0);
    QCOMPARE(view.model()->data(view.model()->index(0, 0)).toString(), QString::fromLatin1("a"));
    QCOMPARE(view.model()->data(view.model()->index(1, 0)).toString(), QString::fromLatin1("b"));
    QCOMPARE(view.model()->data(view.model()->index(0, 1)).toString(), QString::fromLatin1("f"));
    QCOMPARE(view.model()->data(view.model()->index(1, 1)).toString(), QString::fromLatin1("e"));

    view.sortByColumn(-1, Qt::AscendingOrder);
    QCOMPARE(view.horizontalHeader()->sortIndicatorSection(), -1);
    QCOMPARE(view.model()->data(view.model()->index(0, 0)).toString(), QString::fromLatin1("b"));
    QCOMPARE(view.model()->data(view.model()->index(1, 0)).toString(), QString::fromLatin1("d"));
    QCOMPARE(view.model()->data(view.model()->index(0, 1)).toString(), QString::fromLatin1("e"));
    QCOMPARE(view.model()->data(view.model()->index(1, 1)).toString(), QString::fromLatin1("g"));

    // a new 'sortByColumn()' should do a re-sort (e.g. due to the data changed), QTBUG-86268
    view.setModel(&model);
    view.sortByColumn(0, Qt::AscendingOrder);
    QCOMPARE(view.model()->data(view.model()->index(0, 0)).toString(), QString::fromLatin1("a"));
    model.setItem(0, 0, new QStandardItem("x"));
    view.sortByColumn(0, Qt::AscendingOrder);
    QCOMPARE(view.model()->data(view.model()->index(0, 0)).toString(), QString::fromLatin1("b"));
}

void tst_QTableView::scrollTo_data()
{
    QTest::addColumn<QAbstractItemView::ScrollMode>("verticalScrollMode");
    QTest::addColumn<QAbstractItemView::ScrollMode>("horizontalScrollMode");
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("columnCount");
    QTest::addColumn<int>("rowHeight");
    QTest::addColumn<int>("columnWidth");
    QTest::addColumn<int>("hiddenRow");
    QTest::addColumn<int>("hiddenColumn");
    QTest::addColumn<int>("row");
    QTest::addColumn<int>("column");
    QTest::addColumn<int>("rowSpan");
    QTest::addColumn<int>("columnSpan");
    QTest::addColumn<int>("horizontalScroll");
    QTest::addColumn<int>("verticalScroll");
    QTest::addColumn<QAbstractItemView::ScrollHint>("scrollHint");
    QTest::addColumn<int>("expectedHorizontalScroll");
    QTest::addColumn<int>("expectedVerticalScroll");

    QTest::newRow("no hidden, no span, no scroll, per item")
        << QAbstractItemView::ScrollPerItem
        << QAbstractItemView::ScrollPerItem
        << 10 << 10  // table
        << 80 << 80  // size
        << -1 << -1  // hide
        << 0 << 0    // cell
        << 1 << 1    // span
        << 0 << 0    // scroll
        << QAbstractItemView::PositionAtTop
        << 0 << 0;   // expected

    QTest::newRow("no hidden, no span, no scroll, per pixel")
        << QAbstractItemView::ScrollPerPixel
        << QAbstractItemView::ScrollPerPixel
        << 10 << 10  // table
        << 80 << 80  // size
        << -1 << -1  // hide
        << 0 << 0    // cell
        << 1 << 1    // span
        << 0 << 0    // scroll
        << QAbstractItemView::PositionAtTop
        << 0 << 0;   // expected

    QTest::newRow("hidden, no span, no scroll, per item")
        << QAbstractItemView::ScrollPerItem
        << QAbstractItemView::ScrollPerItem
        << 10 << 10  // table
        << 80 << 80  // size
        << 3 << 3    // hide
        << 5 << 5    // cell
        << 1 << 1    // span
        << 0 << 0    // scroll
        << QAbstractItemView::PositionAtTop
        << 4 << 4;   // expected
}

void tst_QTableView::scrollTo()
{
    QFETCH(QAbstractItemView::ScrollMode, horizontalScrollMode);
    QFETCH(QAbstractItemView::ScrollMode, verticalScrollMode);
    QFETCH(int, rowCount);
    QFETCH(int, columnCount);
    QFETCH(int, rowHeight);
    QFETCH(int, columnWidth);
    QFETCH(int, hiddenRow);
    QFETCH(int, hiddenColumn);
    QFETCH(int, row);
    QFETCH(int, column);
    QFETCH(int, rowSpan);
    QFETCH(int, columnSpan);
    QFETCH(int, horizontalScroll);
    QFETCH(int, verticalScroll);
    QFETCH(QAbstractItemView::ScrollHint, scrollHint);
    QFETCH(int, expectedHorizontalScroll);
    QFETCH(int, expectedVerticalScroll);

    QtTestTableModel model(rowCount, columnCount);
    QWidget toplevel;
    setFrameless(&toplevel);
    QtTestTableView view(&toplevel);

    toplevel.show();
    // resizing to this size will ensure that there can ONLY_BE_ONE_CELL inside the view.
    QSize forcedSize(columnWidth * 2, rowHeight * 2);
    view.resize(forcedSize);
    QVERIFY(QTest::qWaitForWindowExposed(&toplevel));
    QTRY_COMPARE(view.size(), forcedSize);

    view.setModel(&model);
    view.setSpan(row, column, rowSpan, columnSpan);
    view.hideRow(hiddenRow);
    view.hideColumn(hiddenColumn);
    view.setHorizontalScrollMode(horizontalScrollMode);
    view.setVerticalScrollMode(verticalScrollMode);

    for (int r = 0; r < rowCount; ++r)
        view.setRowHeight(r, rowHeight);
    for (int c = 0; c < columnCount; ++c)
        view.setColumnWidth(c, columnWidth);

    view.horizontalScrollBar()->setValue(horizontalScroll);
    view.verticalScrollBar()->setValue(verticalScroll);

    QModelIndex index = model.index(row, column);
    QVERIFY(index.isValid());
    view.scrollTo(index, scrollHint);
    QTRY_COMPARE(view.verticalScrollBar()->value(), expectedVerticalScroll);
    QTRY_COMPARE(view.horizontalScrollBar()->value(), expectedHorizontalScroll);
}

void tst_QTableView::indexAt_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("columnCount");

    QTest::addColumn<int>("rowHeight");
    QTest::addColumn<int>("columnWidth");

    QTest::addColumn<int>("hiddenRow");
    QTest::addColumn<int>("hiddenColumn");

    QTest::addColumn<int>("row");
    QTest::addColumn<int>("column");
    QTest::addColumn<int>("rowSpan");
    QTest::addColumn<int>("columnSpan");
    QTest::addColumn<int>("horizontalScroll");
    QTest::addColumn<int>("verticalScroll");
    QTest::addColumn<int>("x");
    QTest::addColumn<int>("y");
    QTest::addColumn<int>("expectedRow");
    QTest::addColumn<int>("expectedColumn");

    QTest::newRow("no hidden, no span, no scroll, (20,20)")
      << 10 << 10  // dim
      << 40 << 40  // size
      << -1 << -1  // hide
      << -1 << -1  // pos
      << 1 << 1    // span
      << 0 << 0    // scroll
      << 20 << 20  // point
      << 0 << 0;   // expected

    QTest::newRow("row hidden, no span, no scroll, at (20,20)")
      << 10 << 10  // dim
      << 40 << 40  // size
      << 0 << -1   // hide
      << -1 << -1  // pos
      << 1 << 1    // span
      << 0 << 0    // scroll
      << 20 << 20  // point
      << 1 << 0;   // expected

    QTest::newRow("col hidden, no span, no scroll, at (20,20)")
      << 10 << 10  // dim
      << 40 << 40  // size
      << -1 << 0   // hide
      << -1 << -1  // pos
      << 1 << 1    // span
      << 0 << 0    // scroll
      << 20 << 20  // point
      << 0 << 1;   // expected

    QTest::newRow("no hidden, row span, no scroll, at (60,20)")
      << 10 << 10  // dim
      << 40 << 40  // size
      << -1 << -1  // hide
      << 0 << 0    // pos
      << 2 << 1    // span
      << 0 << 0    // scroll
      << 20 << 60  // point
      << 0 << 0;   // expected


    QTest::newRow("no hidden, col span, no scroll, at (60,20)")
      << 10 << 10  // dim
      << 40 << 40  // size
      << -1 << -1  // hide
      << 0 << 0    // pos
      << 1 << 2    // span
      << 0 << 0    // scroll
      << 60 << 20  // point
      << 0 << 0;   // expected

    QTest::newRow("no hidden, no span, scroll (5,0), at (20,20)")
      << 20 << 20  // dim
      << 40 << 40  // size
      << -1 << -1  // hide
      << -1 << -1  // pos
      << 1 << 1    // span
      << 5 << 0    // scroll
      << 20 << 20  // point
      << 0 << 5;   // expected

    QTest::newRow("no hidden, no span, scroll (0,5), at (20,20)")
      << 20 << 20  // dim
      << 40 << 40  // size
      << -1 << -1  // hide
      << -1 << -1  // pos
      << 1 << 1    // span
      << 0 << 5    // scroll
      << 20 << 20  // point
      << 5 << 0;   // expected

    QTest::newRow("no hidden, no span, scroll (5,5), at (20,20)")
      << 20 << 20  // dim
      << 40 << 40  // size
      << -1 << -1  // hide
      << -1 << -1  // pos
      << 1 << 1    // span
      << 5 << 5    // scroll
      << 20 << 20  // point
      << 5 << 5;   // expected
}

void tst_QTableView::indexAt()
{
    QFETCH(int, rowCount);
    QFETCH(int, columnCount);
    QFETCH(int, rowHeight);
    QFETCH(int, columnWidth);
    QFETCH(int, hiddenRow);
    QFETCH(int, hiddenColumn);
    QFETCH(int, row);
    QFETCH(int, column);
    QFETCH(int, rowSpan);
    QFETCH(int, columnSpan);
    QFETCH(int, horizontalScroll);
    QFETCH(int, verticalScroll);
    QFETCH(int, x);
    QFETCH(int, y);
    QFETCH(int, expectedRow);
    QFETCH(int, expectedColumn);

    QtTestTableModel model(rowCount, columnCount);
    QWidget toplevel;
    QtTestTableView view(&toplevel);

    toplevel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&toplevel));

    //some styles change the scroll mode in their polish
    view.setHorizontalScrollMode(QAbstractItemView::ScrollPerItem);
    view.setVerticalScrollMode(QAbstractItemView::ScrollPerItem);

    view.setModel(&model);
    view.setSpan(row, column, rowSpan, columnSpan);
    view.hideRow(hiddenRow);
    view.hideColumn(hiddenColumn);

    for (int r = 0; r < rowCount; ++r)
        view.setRowHeight(r, rowHeight);
    for (int c = 0; c < columnCount; ++c)
        view.setColumnWidth(c, columnWidth);

    view.horizontalScrollBar()->setValue(horizontalScroll);
    view.verticalScrollBar()->setValue(verticalScroll);

    QModelIndex index = view.indexAt(QPoint(x, y));
    QTRY_COMPARE(index.row(), expectedRow);
    QTRY_COMPARE(index.column(), expectedColumn);
}

void tst_QTableView::span_data()
{
    QTest::addColumn<int>("rowCount");
    QTest::addColumn<int>("columnCount");
    QTest::addColumn<int>("hiddenRow");
    QTest::addColumn<int>("hiddenColumn");
    QTest::addColumn<int>("row");
    QTest::addColumn<int>("column");
    QTest::addColumn<int>("rowSpan");
    QTest::addColumn<int>("columnSpan");
    QTest::addColumn<int>("expectedRowSpan");
    QTest::addColumn<int>("expectedColumnSpan");
    QTest::addColumn<bool>("clear");

    QTest::newRow("top left 2x2")
      << 10 << 10
      << -1 << -1
      << 0 << 0
      << 2 << 2
      << 2 << 2
      << false;

    QTest::newRow("top left 1x2")
      << 10 << 10
      << 3 << 3
      << 0 << 0
      << 1 << 2
      << 1 << 2
      << false;

    QTest::newRow("top left 2x1")
      << 10 << 10
      << -1 << -1
      << 0 << 0
      << 2 << 1
      << 2 << 1
      << false;

  /* This makes no sens.
    QTest::newRow("top left 2x0")
      << 10 << 10
      << -1 << -1
      << 0 << 0
      << 2 << 0
      << 2 << 0
      << false;

    QTest::newRow("top left 0x2")
      << 10 << 10
      << -1 << -1
      << 0 << 0
      << 0 << 2
      << 0 << 2
      << false;*/

    QTest::newRow("invalid 2x2")
      << 10 << 10
      << -1 << -1
      << -1 << -1
      << 2 << 2
      << 1 << 1
      << false;

    QTest::newRow("top left 2x2")
      << 10 << 10
      << -1 << -1
      << 0 << 0
      << 2 << 2
      << 2 << 2
      << false;

    QTest::newRow("bottom right 2x2")
      << 10 << 10
      << -1 << -1
      << 8 << 8
      << 2 << 2
      << 2 << 2
      << false;

    QTest::newRow("invalid span 2x2")
      << 10 << 10
      << -1 << -1
      << 8 << 8
      << 2 << 2
      << 2 << 2
      << false;

    QTest::newRow("invalid span 3x3")
      << 10 << 10
      << -1 << -1
      << 6 << 6
      << 3 << 3
      << 2 << 3
      << true;

}

void tst_QTableView::span()
{
    QFETCH(int, rowCount);
    QFETCH(int, columnCount);
    QFETCH(int, hiddenRow);
    QFETCH(int, hiddenColumn);
    QFETCH(int, row);
    QFETCH(int, column);
    QFETCH(int, rowSpan);
    QFETCH(int, columnSpan);
    QFETCH(int, expectedRowSpan);
    QFETCH(int, expectedColumnSpan);
    QFETCH(bool, clear);

    QtTestTableModel model(rowCount, columnCount);
    QtTestTableView view;

    view.setModel(&model);
    view.show();

    view.setSpan(row, column, rowSpan, columnSpan);
    if (clear) {
        model.removeLastRow();
        model.removeLastRow();
        view.update();
    }

    view.hideRow(hiddenRow);
    view.hideColumn(hiddenColumn);
    view.show();

    QCOMPARE(view.rowSpan(row, column), expectedRowSpan);
    QCOMPARE(view.columnSpan(row, column), expectedColumnSpan);

    if (hiddenRow > -1) {
        QModelIndex hidden = model.index(hiddenRow, columnCount - 1);
        QVERIFY(view.isIndexHidden(hidden));
    }

    if (hiddenColumn > -1) {
        QModelIndex hidden = model.index(rowCount - 1, hiddenColumn);
        QVERIFY(view.isIndexHidden(hidden));
    }

    view.clearSpans();
    QCOMPARE(view.rowSpan(row, column), 1);
    QCOMPARE(view.columnSpan(row, column), 1);

    VERIFY_SPANS_CONSISTENCY(&view);
}

void tst_QTableView::spans_data()
{
    QTest::addColumn<int>("rows");
    QTest::addColumn<int>("columns");
    QTest::addColumn<SpanList>("spans");
    QTest::addColumn<bool>("hideRowLastRowOfFirstSpan");
    QTest::addColumn<QPoint>("pos");
    QTest::addColumn<int>("expectedRowSpan");
    QTest::addColumn<int>("expectedColumnSpan");

    QTest::newRow("1x3 span, query 3,0")
      << 5 << 5
      << (SpanList() << QRect(3, 0, 1, 3))
      << false //no hidden row
      << QPoint(3, 0)
      << 1
      << 3;

    QTest::newRow("1x3 span, query 3,1")
      << 5 << 5
      << (SpanList() << QRect(3, 0, 1, 3))
      << false //no hidden row
      << QPoint(3, 1)
      << 1
      << 3;

    QTest::newRow("1x3 span, query 3,2")
      << 5 << 5
      << (SpanList() << QRect(3, 0, 1, 3))
      << false //no hidden row
      << QPoint(3, 2)
      << 1
      << 3;

    QTest::newRow("two 1x2 spans at the same column, query at 3,0")
      << 5 << 5
      << (SpanList() << QRect(3, 0, 1, 2) << QRect(4, 0, 1, 2))
      << false //no hidden row
      << QPoint(3, 0)
      << 1
      << 2;

    QTest::newRow("two 1x2 spans at the same column, query at 4,0")
      << 5 << 5
      << (SpanList() << QRect(3, 0, 1, 2) << QRect(4, 0, 1, 2))
      << false //no hidden row
      << QPoint(4, 0)
      << 1
      << 2;

    QTest::newRow("how to order spans (1,1)")
      << 5 << 5
      << (SpanList() << QRect(1, 1, 3, 1) << QRect(1, 2, 2, 1))
      << false //no hidden row
      << QPoint(1, 1)
      << 3
      << 1;

    QTest::newRow("how to order spans (2,1)")
      << 5 << 5
      << (SpanList() << QRect(1, 1, 3, 1) << QRect(1, 2, 2, 1))
      << false //no hidden row
      << QPoint(2, 1)
      << 3
      << 1;

    QTest::newRow("how to order spans (3,1)")
      << 5 << 5
      << (SpanList() << QRect(1, 1, 3, 1) << QRect(1, 2, 2, 1))
      << false //no hidden row
      << QPoint(3, 1)
      << 3
      << 1;

    QTest::newRow("how to order spans (1,2)")
      << 5 << 5
      << (SpanList() << QRect(1, 1, 3, 1) << QRect(1, 2, 2, 1))
      << false //no hidden row
      << QPoint(1, 2)
      << 2
      << 1;

    QTest::newRow("how to order spans (2,2)")
      << 5 << 5
      << (SpanList() << QRect(1, 1, 3, 1) << QRect(1, 2, 2, 1))
      << false //no hidden row
      << QPoint(2, 2)
      << 2
      << 1;

    QTest::newRow("spans with hidden rows")
      << 3 << 2
      << (SpanList() << QRect(0, 0, 2, 2) << QRect(2, 0, 1, 2))
      << true //we hide the last row of the first span
      << QPoint(2, 0)
      << 1
      << 2;

    QTest::newRow("QTBUG-6004: No failing assertion, then it passes.")
      << 5 << 5
      << (SpanList() << QRect(0, 0, 2, 2) << QRect(0, 0, 1, 1))
      << false
      << QPoint(0, 0)
      << 1
      << 1;

    QTest::newRow("QTBUG-6004 (follow-up): No failing assertion, then it passes.")
      << 10 << 10
      << (SpanList() << QRect(2, 2, 1, 3) << QRect(2, 2, 1, 1))
      << false
      << QPoint(0, 0)
      << 1
      << 1;

    QTest::newRow("QTBUG-9631: remove one span")
      << 10 << 10
      << (SpanList() << QRect(1, 1, 2, 1) << QRect(2, 2, 2, 2) << QRect(1, 1, 1, 1))
      << false
      << QPoint(1, 1)
      << 1
      << 1;
}

void tst_QTableView::spans()
{
    QFETCH(int, rows);
    QFETCH(int, columns);
    QFETCH(const SpanList, spans);
    QFETCH(bool, hideRowLastRowOfFirstSpan);
    QFETCH(QPoint, pos);
    QFETCH(int, expectedRowSpan);
    QFETCH(int, expectedColumnSpan);

    QtTestTableModel model(rows, columns);
    QtTestTableView view;

    view.setModel(&model);
    view.show();

    for (const auto &sp : spans)
        view.setSpan(sp.x(), sp.y(), sp.width(), sp.height());

    if (hideRowLastRowOfFirstSpan) {
        view.setRowHidden(spans.at(0).bottom(), true);
        //we check that the span didn't break the visual rects of the model indexes
        QRect first = view.visualRect( model.index(spans.at(0).top(), 0));
        QRect next = view.visualRect( model.index(spans.at(0).bottom() + 1, 0));
        QVERIFY(first.intersected(next).isEmpty());
    }

    QCOMPARE(view.columnSpan(pos.x(), pos.y()), expectedColumnSpan);
    QCOMPARE(view.rowSpan(pos.x(), pos.y()), expectedRowSpan);

    VERIFY_SPANS_CONSISTENCY(&view);
}

void tst_QTableView::spansAfterRowInsertion()
{
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"), Qt::CaseInsensitive))
        QSKIP("Wayland: This fails. Figure out why.");

    QtTestTableModel model(10, 10);
    QtTestTableView view;
    view.setModel(&model);
    view.setSpan(3, 3, 3, 3);
    view.show();
    QVERIFY(QTest::qWaitForWindowActive(&view));

    // Insertion before the span only shifts the span.
    view.model()->insertRows(0, 2);
    QCOMPARE(view.rowSpan(3, 3), 1);
    QCOMPARE(view.columnSpan(3, 3), 1);
    QCOMPARE(view.rowSpan(5, 3), 3);
    QCOMPARE(view.columnSpan(5, 3), 3);

    // Insertion happens before the given row, so it only shifts the span also.
    view.model()->insertRows(5, 2);
    QCOMPARE(view.rowSpan(5, 3), 1);
    QCOMPARE(view.columnSpan(5, 3), 1);
    QCOMPARE(view.rowSpan(7, 3), 3);
    QCOMPARE(view.columnSpan(7, 3), 3);

    // Insertion inside the span expands it.
    view.model()->insertRows(8, 2);
    QCOMPARE(view.rowSpan(7, 3), 5);
    QCOMPARE(view.columnSpan(7, 3), 3);

    // Insertion after the span does nothing to it.
    view.model()->insertRows(12, 2);
    QCOMPARE(view.rowSpan(7, 3), 5);
    QCOMPARE(view.columnSpan(7, 3), 3);

    VERIFY_SPANS_CONSISTENCY(&view);
}

void tst_QTableView::spansAfterColumnInsertion()
{
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"), Qt::CaseInsensitive))
        QSKIP("Wayland: This fails. Figure out why.");

    QtTestTableModel model(10, 10);
    QtTestTableView view;
    view.setModel(&model);
    view.setSpan(3, 3, 3, 3);
    view.show();
    QVERIFY(QTest::qWaitForWindowActive(&view));

    // Insertion before the span only shifts the span.
    view.model()->insertColumns(0, 2);
    QCOMPARE(view.rowSpan(3, 3), 1);
    QCOMPARE(view.columnSpan(3, 3), 1);
    QCOMPARE(view.rowSpan(3, 5), 3);
    QCOMPARE(view.columnSpan(3, 5), 3);

    // Insertion happens before the given column, so it only shifts the span also.
    view.model()->insertColumns(5, 2);
    QCOMPARE(view.rowSpan(3, 5), 1);
    QCOMPARE(view.columnSpan(3, 5), 1);
    QCOMPARE(view.rowSpan(3, 7), 3);
    QCOMPARE(view.columnSpan(3, 7), 3);

    // Insertion inside the span expands it.
    view.model()->insertColumns(8, 2);
    QCOMPARE(view.rowSpan(3, 7), 3);
    QCOMPARE(view.columnSpan(3, 7), 5);

    // Insertion after the span does nothing to it.
    view.model()->insertColumns(12, 2);
    QCOMPARE(view.rowSpan(3, 7), 3);
    QCOMPARE(view.columnSpan(3, 7), 5);

    VERIFY_SPANS_CONSISTENCY(&view);
}

void tst_QTableView::spansAfterRowRemoval()
{
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"), Qt::CaseInsensitive))
        QSKIP("Wayland: This fails. Figure out why.");

    QtTestTableModel model(10, 10);
    QtTestTableView view;
    view.setModel(&model);

    static const QRect spans[] = {
        {0, 1, 1, 2},
        {1, 2, 1, 2},
        {2, 2, 1, 5},
        {2, 8, 1, 2},
        {3, 4, 1, 2},
        {4, 4, 1, 4},
        {5, 6, 1, 3},
        {6, 7, 1, 3}
    };
    for (const QRect &span : spans)
        view.setSpan(span.top(), span.left(), span.height(), span.width());

    view.show();
    QVERIFY(QTest::qWaitForWindowActive(&view));
    view.model()->removeRows(3, 3);

    static const QRect expectedSpans[] = {
        {0, 1, 1, 2},
        {1, 2, 1, 1},
        {2, 2, 1, 2},
        {2, 5, 1, 2},
        {3, 4, 1, 1},
        {4, 3, 1, 2},
        {5, 3, 1, 3},
        {6, 4, 1, 3}
    };
    for (const QRect &span : expectedSpans) {
        QCOMPARE(view.columnSpan(span.top(), span.left()), span.width());
        QCOMPARE(view.rowSpan(span.top(), span.left()), span.height());
    }

    VERIFY_SPANS_CONSISTENCY(&view);
}

void tst_QTableView::spansAfterColumnRemoval()
{
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"), Qt::CaseInsensitive))
        QSKIP("Wayland: This fails. Figure out why.");

    QtTestTableModel model(10, 10);
    QtTestTableView view;
    view.setModel(&model);

    // Same set as above just swapping columns and rows.
    static const QRect spans[] = {
        {0, 1, 1, 2},
        {1, 2, 1, 2},
        {2, 2, 1, 5},
        {2, 8, 1, 2},
        {3, 4, 1, 2},
        {4, 4, 1, 4},
        {5, 6, 1, 3},
        {6, 7, 1, 3}
    };
    for (const QRect &span : spans)
      view.setSpan(span.left(), span.top(), span.width(), span.height());

    view.show();
    QVERIFY(QTest::qWaitForWindowActive(&view));
    view.model()->removeColumns(3, 3);

    static const QRect expectedSpans[] = {
        {0, 1, 1, 2},
        {1, 2, 1, 1},
        {2, 2, 1, 2},
        {2, 5, 1, 2},
        {3, 4, 1, 1},
        {4, 3, 1, 2},
        {5, 3, 1, 3},
        {6, 4, 1, 3}
    };
    for (const QRect &span : expectedSpans) {
        QCOMPARE(view.columnSpan(span.left(), span.top()), span.height());
        QCOMPARE(view.rowSpan(span.left(), span.top()), span.width());
    }

    VERIFY_SPANS_CONSISTENCY(&view);
}

void tst_QTableView::editSpanFromDirections_data()
{
    QTest::addColumn<KeyList>("keyPresses");
    QTest::addColumn<QSharedPointer<QStandardItemModel>>("model");
    QTest::addColumn<int>("row");
    QTest::addColumn<int>("column");
    QTest::addColumn<int>("rowSpan");
    QTest::addColumn<int>("columnSpan");
    QTest::addColumn<QModelIndex>("expectedVisualCursorIndex");
    QTest::addColumn<QModelIndex>("expectedEditedIndex");

    /* x = the cell that should be edited
       c = the cell that should actually be the current index
       +---+---+
       |   |   |
       +---+---+
       |   | x |
       +---+   +
       |   | c |
       +---+---+
       |   | ^ |
       +---+---+ */
    KeyList keyPresses {Qt::Key_Right, Qt::Key_PageDown, Qt::Key_Up};
    QSharedPointer<QStandardItemModel> model(new QStandardItemModel(4, 2));
    QTest::newRow("row span, bottom up")
        << keyPresses << model << 1 << 1 << 2 << 1 << model->index(2, 1) << model->index(1, 1);

    /* +---+---+
       |   | v |
       +---+---+
       |   |x,c|
       +---+   +
       |   |   |
       +---+---+
       |   |   |
       +---+---+ */
    keyPresses = {Qt::Key_Right, Qt::Key_Down};
    model = QSharedPointer<QStandardItemModel>::create(4, 2);
    QTest::newRow("row span, top down")
        << keyPresses << model << 1 << 1 << 2 << 1 << model->index(1, 1) << model->index(1, 1);

    /* +---+---+---+
       |   |   |   |
       +---+---+---+
       |   |x,c| < |
       +---+   +---+
       |   |   |   |
       +---+---+---+ */
    keyPresses = {Qt::Key_End, Qt::Key_Down, Qt::Key_Left};
    model = QSharedPointer<QStandardItemModel>::create(3, 3);
    QTest::newRow("row span, right to left")
        << keyPresses << model << 1 << 1 << 2 << 1 << model->index(1, 1) << model->index(1, 1);

    /* +---+---+---+
       |   |   |   |
       +---+---+---+
       |   | x |   |
       +---+   +---+
       | > | c |   |
       +---+---+---+ */
    keyPresses = {Qt::Key_PageDown, Qt::Key_Right};
    model = QSharedPointer<QStandardItemModel>::create(3, 3);
    QTest::newRow("row span, left to right")
        << keyPresses << model << 1 << 1 << 2 << 1 << model->index(2, 1) << model->index(1, 1);

    /* +---+---+---+
       |   |   |   |
       +---+---+---+
       |x,c        |
       +---+---+---+
       | ^ |   |   |
       +---+---+---+ */
    keyPresses = {Qt::Key_PageDown, Qt::Key_Up};
    model = QSharedPointer<QStandardItemModel>::create(3, 3);
    QTest::newRow("col span, bottom up")
        << keyPresses << model << 1 << 0 << 1 << 3 << model->index(1, 0) << model->index(1, 0);

    /* +---+---+---+
       |   |   |   |
       +---+---+---+
       | x   c     |
       +---+---+---+
       |   | ^ |   |
       +---+---+---+ */
    keyPresses = {Qt::Key_PageDown, Qt::Key_Right, Qt::Key_Up};
    model = QSharedPointer<QStandardItemModel>::create(3, 3);
    QTest::newRow("col span, bottom up #2")
        << keyPresses << model << 1 << 0 << 1 << 3 << model->index(1, 1) << model->index(1, 0);

    /* +---+---+---+
       |   |   | v |
       +---+---+---+
       | x       c |
       +---+---+---+
       |   |   |   |
       +---+---+---+ */
    keyPresses = {Qt::Key_End, Qt::Key_Down};
    model = QSharedPointer<QStandardItemModel>::create(3, 3);
    QTest::newRow("col span, top down")
        << keyPresses << model << 1 << 0 << 1 << 3 << model->index(1, 2) << model->index(1, 0);
}

class TableViewWithCursorExposed : public QTableView
{
public:
    using QTableView::QTableView;

    QModelIndex visualCursorIndex()
    {
        QTableViewPrivate *d = static_cast<QTableViewPrivate*>(qt_widget_private(this));
        return d->model->index(d->visualCursor.y(), d->visualCursor.x());
    }
};

void tst_QTableView::editSpanFromDirections()
{
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"), Qt::CaseInsensitive))
        QSKIP("Wayland: This fails. Figure out why.");

    QFETCH(const KeyList, keyPresses);
    QFETCH(QSharedPointer<QStandardItemModel>, model);
    QFETCH(int, row);
    QFETCH(int, column);
    QFETCH(int, rowSpan);
    QFETCH(int, columnSpan);
    QFETCH(QModelIndex, expectedVisualCursorIndex);
    QFETCH(QModelIndex, expectedEditedIndex);

    TableViewWithCursorExposed view;
    view.setModel(model.data());
    // we have to make sure that PgUp/PgDown can scroll to the bottom/top
    view.resize(view.horizontalHeader()->length() + 50,
                view.verticalHeader()->length() + 50);
    view.setSpan(row, column, rowSpan, columnSpan);
    view.show();
    QVERIFY(QTest::qWaitForWindowActive(&view));

    for (Qt::Key key : keyPresses)
        QTest::keyClick(&view, key);
    QCOMPARE(view.visualCursorIndex(), expectedVisualCursorIndex);
    QCOMPARE(view.selectionModel()->currentIndex(), expectedEditedIndex);

    QTest::keyClick(&view, Qt::Key_X);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Enter);
    QTRY_COMPARE(view.model()->data(expectedEditedIndex).toString(), QLatin1String("x"));
}

class Model : public QAbstractTableModel
{
    Q_OBJECT
public:
    using QAbstractTableModel::QAbstractTableModel;

    int rowCount(const QModelIndex &) const override
    {
        return rows;
    }
    int columnCount(const QModelIndex &) const override
    {
        return columns;
    }
    QVariant data(const QModelIndex &, int) const override
    {
        return QVariant();
    }
    void res()
    {
        beginResetModel();
        endResetModel();
    }

    int rows = 0;
    int columns = 0;
};

void tst_QTableView::checkHeaderReset()
{
    QTableView view;
    Model m;
    m.rows = 3;
    m.columns = 3;
    view.setModel(&m);

    m.rows = 4;
    m.columns = 4;
    m.res();
    QCOMPARE(view.horizontalHeader()->count(), 4);
}

void tst_QTableView::checkHeaderMinSize()
{
    //tests if the minimumsize is of a header is taken into account
    //while computing QTableView geometry. For that we test the position of the
    //viewport.
    QTableView view;
    QStringListModel m;
    m.setStringList({QLatin1String("one cell is enough")});
    view.setModel(&m);

    //setting the minimum height on the horizontal header
    //and the minimum width on the vertical header
    view.horizontalHeader()->setMinimumHeight(50);
    view.verticalHeader()->setMinimumWidth(100);

    view.show();

    QVERIFY( view.verticalHeader()->y() >= view.horizontalHeader()->minimumHeight());
    QVERIFY( view.horizontalHeader()->x() >= view.verticalHeader()->minimumWidth());
}

void tst_QTableView::resizeToContents()
{
    //checks that the resize to contents is consistent
    QTableWidget table(2,3);
    QTableWidget table2(2,3);
    QTableWidget table3(2,3);


    table.setHorizontalHeaderItem(0, new QTableWidgetItem("A Lot of text here: BLA BLA BLA"));
    table2.setHorizontalHeaderItem(0, new QTableWidgetItem("A Lot of text here: BLA BLA BLA"));
    table3.setHorizontalHeaderItem(0, new QTableWidgetItem("A Lot of text here: BLA BLA BLA"));
    table.horizontalHeader()->setVisible(false);
    table2.horizontalHeader()->setVisible(false);
    table.verticalHeader()->setVisible(false);
    table2.verticalHeader()->setVisible(false);


    for (int i = 0; i < table.columnCount(); i++)
        table.resizeColumnToContents(i);
    for (int i = 0; i < table.rowCount(); i++)
        table.resizeRowToContents(i);
    table2.resizeColumnsToContents();
    table2.resizeRowsToContents();
    table3.resizeColumnsToContents();
    table3.resizeRowsToContents();

    //now let's check the row/col sizes
    for (int i = 0; i < table.columnCount(); i++) {
        QCOMPARE(table.columnWidth(i), table2.columnWidth(i));
        QCOMPARE(table2.columnWidth(i), table3.columnWidth(i));
    }
    for (int i = 0; i < table.rowCount(); i++) {
        QCOMPARE(table.rowHeight(i), table2.rowHeight(i));
        QCOMPARE(table2.rowHeight(i), table3.rowHeight(i));
    }

}


class SpanModel : public QAbstractTableModel
{
public:
    SpanModel(bool sectionsMoved)
        : _sectionsMoved(sectionsMoved)
    {}
    int columnCount(const QModelIndex & = {}) const override { return 2; }
    int rowCount(const QModelIndex & = {}) const override { return 1; }
    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole)
            return QVariant();
        const int col = _sectionsMoved ? 1 - idx.column() : idx.column();
        if (col == 0)
            return "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.";
        return QVariant();
    }
private:
    bool _sectionsMoved;
};


void tst_QTableView::resizeToContentsSpans()
{
    SpanModel model1(false);
    SpanModel model2(true);
    QTableView view1, view2, view3;
    view1.setModel(&model1);
    view2.setModel(&model2);
    view2.horizontalHeader()->moveSection(0, 1);
    view3.setModel(&model1);

    view1.setSpan(0, 0, 1, 2);
    view2.setSpan(0, 1, 1, 2);
    view1.show();
    view2.show();
    view3.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view1));
    QVERIFY(QTest::qWaitForWindowExposed(&view2));
    QVERIFY(QTest::qWaitForWindowExposed(&view3));
    view1.setColumnWidth(0, 100);
    view1.setColumnWidth(1, 100);
    view2.setColumnWidth(0, 100);
    view2.setColumnWidth(1, 100);
    view3.setColumnWidth(0, 200);

    view1.resizeRowToContents(0);
    view2.resizeRowToContents(0);
    view3.resizeRowToContents(0);
    QCOMPARE(view1.rowHeight(0), view3.rowHeight(0));
    QCOMPARE(view2.rowHeight(0), view3.rowHeight(0));

    view3.resizeColumnToContents(0);
    view3.resizeRowToContents(0);
    // height should be only 1 text line for easy testing
    view1.setRowHeight(0, view3.verticalHeader()->sectionSize(0));
    view2.setRowHeight(0, view3.verticalHeader()->sectionSize(0));
    view1.resizeColumnToContents(0);
    view2.resizeColumnToContents(1);
    QCOMPARE(view1.columnWidth(0), view3.columnWidth(0) - view1.columnWidth(1));
    QCOMPARE(view2.columnWidth(0), view3.columnWidth(0) - view2.columnWidth(1));
}

void tst_QTableView::resizeToContentsEarly()
{
    QStringListModel model;
    QTableView view;

    // connect to the model before setting it on the view
    connect(&model, &QStringListModel::modelReset, &model, [&view]{
        view.resizeColumnsToContents();
    });
    connect(&model, &QStringListModel::modelReset, &model, [&view]{
        view.resizeRowsToContents();
    });

    // the view only connects now to the model's signals, so responds to the
    // reset signal *after* the lambdas above
    view.setModel(&model);

    QStringList data(200, QString("Hello World"));
    model.setStringList(data);

    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    view.verticalScrollBar()->setValue(view.verticalScrollBar()->maximum());

    data = data.sliced(data.size() / 2);
    model.setStringList(data);
}

QT_BEGIN_NAMESPACE
extern bool Q_WIDGETS_EXPORT qt_tab_all_widgets(); // qapplication.cpp
QT_END_NAMESPACE

void tst_QTableView::tabFocus()
{
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"), Qt::CaseInsensitive))
        QSKIP("Wayland: This fails. Figure out why.");

    if (!qt_tab_all_widgets())
        QSKIP("This test requires full keyboard control to be enabled.");

    // QTableView enables tabKeyNavigation by default, but you should be able
    // to change focus on an empty table view, or on a table view that doesn't
    // have this property set.
    QWidget window;
    window.resize(200, 200);

    QTableView *view = new QTableView(&window);
    QLineEdit *edit = new QLineEdit(&window);

    window.show();
    QApplicationPrivate::setActiveWindow(&window);
    window.setFocus();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    // window
    QVERIFY(window.hasFocus());
    QVERIFY(!view->hasFocus());
    QVERIFY(!edit->hasFocus());

    for (int i = 0; i < 2; ++i) {
        // tab to view
        QTest::keyPress(QApplication::focusWidget(), Qt::Key_Tab);
        QTRY_VERIFY(!window.hasFocus());
        QVERIFY(view->hasFocus());
        QVERIFY(!edit->hasFocus());

        // tab to edit
        QTest::keyPress(QApplication::focusWidget(), Qt::Key_Tab);
        QTRY_VERIFY(edit->hasFocus());
        QVERIFY(!window.hasFocus());
        QVERIFY(!view->hasFocus());
    }

    // backtab to view
    QTest::keyPress(QApplication::focusWidget(), Qt::Key_Backtab);
    QTRY_VERIFY(view->hasFocus());
    QVERIFY(!window.hasFocus());
    QVERIFY(!edit->hasFocus());

    // backtab to edit
    QTest::keyPress(QApplication::focusWidget(), Qt::Key_Backtab);
    QTRY_VERIFY(edit->hasFocus());
    QVERIFY(!window.hasFocus());
    QVERIFY(!view->hasFocus());

    QStandardItemModel model;
    view->setModel(&model);

    // backtab to view
    QTest::keyPress(QApplication::focusWidget(), Qt::Key_Backtab);
    QTRY_VERIFY(view->hasFocus());
    QVERIFY(!window.hasFocus());
    QVERIFY(!edit->hasFocus());

    // backtab to edit
    QTest::keyPress(QApplication::focusWidget(), Qt::Key_Backtab);
    QTRY_VERIFY(edit->hasFocus());
    QVERIFY(!window.hasFocus());
    QVERIFY(!view->hasFocus());

    model.insertRow(0, new QStandardItem("Hei"));
    model.insertRow(0, new QStandardItem("Hei"));
    model.insertRow(0, new QStandardItem("Hei"));

    // backtab to view
    QTest::keyPress(QApplication::focusWidget(), Qt::Key_Backtab);
    QTRY_VERIFY(view->hasFocus());
    QVERIFY(!window.hasFocus());
    QVERIFY(!edit->hasFocus());

    // backtab to edit doesn't work
    QTest::keyPress(QApplication::focusWidget(), Qt::Key_Backtab);
    QVERIFY(!window.hasFocus());
    QVERIFY(view->hasFocus());
    QVERIFY(!edit->hasFocus());

    view->setTabKeyNavigation(false);

    // backtab to edit
    QTest::keyPress(QApplication::focusWidget(), Qt::Key_Backtab);
    QTRY_VERIFY(edit->hasFocus());
    QVERIFY(!window.hasFocus());
    QVERIFY(!view->hasFocus());

    QTest::keyPress(QApplication::focusWidget(), Qt::Key_Tab);
    QTRY_VERIFY(view->hasFocus());
    QTest::keyPress(QApplication::focusWidget(), Qt::Key_Tab);
    QTRY_VERIFY(edit->hasFocus());
}

class BigModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override
    {
        if (role == Qt::DisplayRole)
            return QString::number(index.column()) + QLatin1String(" - ") + QString::number(index.row());
        return QVariant();
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        Q_UNUSED(parent);
        return 10000000;
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        Q_UNUSED(parent);
        return 20000000;
    }
};

void tst_QTableView::bigModel()
{
    //should not crash
    QTableView view;
    BigModel model;
    view.setModel(&model);
    view.show();
    view.setSpan(10002,10002,6,6);
    QTest::qWait(100);
    view.resize(1000,1000);
    QTest::qWait(100);
    view.scrollTo(model.index(10010,10010));
    QTest::qWait(100);
}

void tst_QTableView::selectionSignal()
{
    QtTestTableModel model(10, 10);
    QtTestTableView view;
    view.checkSignalOrder = true;
    view.setModel(&model);
    view.resize(200, 200);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, {}, view.visualRect(model.index(2, 0)).center());
}

void tst_QTableView::setCurrentIndex()
{
    QtTestTableModel model(4, 4);
    QtTestTableView view;
    view.setModel(&model);

    // submit() slot should be called in model when current row changes
    view.setCurrentIndex(model.index(0,0));
    QCOMPARE(model.submit_count, 1);
    view.setCurrentIndex(model.index(0,2));
    QCOMPARE(model.submit_count, 1);
    view.setCurrentIndex(model.index(1,0));
    QCOMPARE(model.submit_count, 2);
    view.setCurrentIndex(model.index(3,3));
    QCOMPARE(model.submit_count, 3);
    view.setCurrentIndex(model.index(0,1));
    QCOMPARE(model.submit_count, 4);
    view.setCurrentIndex(model.index(0,0));
    QCOMPARE(model.submit_count, 4);
}

void tst_QTableView::checkIntersectedRect_data()
{
    QTest::addColumn<QtTestTableModel *>("model");
    QTest::addColumn<QList<QModelIndex>>("changedIndexes");
    QTest::addColumn<bool>("isEmpty");
    QTest::addColumn<bool>("swapFirstAndLastIndexRow");  // for QHeaderView::sectionsMoved()
    QTest::addColumn<bool>("swapFirstAndLastIndexColumn");  // for QHeaderView::sectionsMoved()
    QTest::addColumn<Qt::LayoutDirection>("layoutDirection");
    QTest::addColumn<int>("hiddenRow");
    QTest::addColumn<int>("hiddenCol");
    const auto testName = [](const QByteArray &prefix, Qt::LayoutDirection dir, bool r, bool c)
    {
        const char *strDir = dir == Qt::LeftToRight ? ", LeftToRight" : ", RightToLeft";
        const char *strRow = r ? ", rowsSwapped" : "";
        const char *strCol = c ? ", colsSwapped" : "";
        return prefix + strDir + strRow + strCol;
    };
    for (int i = 0; i < 2; ++i) {
        const Qt::LayoutDirection dir(i == 0 ? Qt::LeftToRight : Qt::RightToLeft);
        for (int j = 0; j < 4; ++j) {
            const bool swapRow = ((j & 1) == 1);
            const bool swapColumn = ((j & 2) == 2);
            {
                QtTestTableModel *model = new QtTestTableModel(10, 3);
                QTest::newRow(testName("multiple columns", dir, swapRow, swapColumn).data())
                        << model << QList<QModelIndex>({ model->index(0, 0), model->index(0, 1) })
                        << false << swapRow << swapColumn << dir << -1 << -1;
            }
            {
                QtTestTableModel *model = new QtTestTableModel(10, 3);
                QTest::newRow(testName("multiple rows", dir, swapRow, swapColumn).data())
                        << model
                        << QList<QModelIndex>(
                                   { model->index(0, 0), model->index(1, 0), model->index(2, 0) })
                        << false << swapRow << swapColumn << dir << -1 << -1;
            }
            {
                QtTestTableModel *model = new QtTestTableModel(10, 3);
                QTest::newRow(testName("hidden row", dir, swapRow, swapColumn).data())
                        << model << QList<QModelIndex>({ model->index(3, 0), model->index(3, 1) })
                        << true << swapRow << swapColumn << dir << 3 << -1;
            }
            {
                QtTestTableModel *model = new QtTestTableModel(50, 2);
                QTest::newRow(testName("row outside viewport", dir, swapRow, swapColumn).data())
                        << model << QList<QModelIndex>({ model->index(49, 0), model->index(49, 1) })
                        << true << swapRow << swapColumn << dir << -1 << -1;
            }
        }
    }
}

void tst_QTableView::checkIntersectedRect()
{
    QFETCH(QtTestTableModel *, model);
    QFETCH(const QList<QModelIndex>, changedIndexes);
    QFETCH(bool, isEmpty);
    QFETCH(bool, swapFirstAndLastIndexRow);
    QFETCH(bool, swapFirstAndLastIndexColumn);
    QFETCH(Qt::LayoutDirection, layoutDirection);
    QFETCH(int, hiddenRow);
    QFETCH(int, hiddenCol);

    QtTestTableView view;
    model->setParent(&view);
    view.setLayoutDirection(layoutDirection);
    view.setModel(model);
    view.resize(400, 400);
    view.show();
    if (hiddenRow >= 0)
        view.hideRow(hiddenRow);
    if (hiddenCol >= 0)
        view.hideRow(hiddenCol);
    if (swapFirstAndLastIndexRow)
        view.verticalHeader()->swapSections(changedIndexes.first().row(), changedIndexes.last().row());
    if (swapFirstAndLastIndexColumn)
        view.horizontalHeader()->swapSections(changedIndexes.first().column(), changedIndexes.last().column());

    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto toString = [](const QModelIndex &idx)
    {
        return QStringLiteral("idx: %1/%2").arg(idx.row()).arg(idx.column());
    };

    view.m_intersectecRect = QRect();
    emit view.model()->dataChanged(changedIndexes.first(), changedIndexes.last());
    if (isEmpty) {
        QVERIFY(view.m_intersectecRect.isEmpty());
    } else if (!changedIndexes.first().isValid()) {
        QCOMPARE(view.m_intersectecRect, view.viewport()->rect());
    } else {
        const auto parent = changedIndexes.first().parent();
        const int rCount = view.model()->rowCount(parent);
        const int cCount = view.model()->columnCount(parent);
        for (int r = 0; r < rCount; ++r) {
            for (int c = 0; c < cCount; ++c) {
                const QModelIndex &idx = view.model()->index(r, c, parent);
                const auto rect = view.visualRect(idx);
                if (changedIndexes.contains(idx))
                    QVERIFY2(view.m_intersectecRect.contains(rect), qPrintable(toString(idx)));
                else
                    QVERIFY2(!view.m_intersectecRect.contains(rect), qPrintable(toString(idx)));
            }
        }
    }
}

class task173773_EventFilter : public QObject
{
    int paintEventCount_ = 0;
public:
    using QObject::QObject;
    int paintEventCount() const { return paintEventCount_; }
private:
    bool eventFilter(QObject *obj, QEvent *e) override
    {
        Q_UNUSED(obj);
        if (e->type() == QEvent::Paint)
            ++paintEventCount_;
        return false;
    }
};

void tst_QTableView::task173773_updateVerticalHeader()
{
    QStandardItemModel model(2, 1);
    model.setData(model.index(0, 0), 0);
    model.setData(model.index(1, 0), 1);

    QSortFilterProxyModel proxyModel;
    proxyModel.setSourceModel(&model);

    QTableView view;
    view.setModel(&proxyModel);
    view.setSortingEnabled(true);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    view.sortByColumn(0, Qt::AscendingOrder);
    QTest::qWait(100);

    task173773_EventFilter eventFilter;
    view.verticalHeader()->viewport()->installEventFilter(&eventFilter);

    view.sortByColumn(0, Qt::DescendingOrder);
    QTest::qWait(100);

    // ### note: this test may occasionally pass even if the bug is present!
    QVERIFY(eventFilter.paintEventCount() > 0);
}

void tst_QTableView::task227953_setRootIndex()
{
    QTableView tableView;

    //model = tree with two items with tables as children
    QStandardItemModel model;
    QStandardItem item1, item2;
    model.appendColumn(QList<QStandardItem*>() << &item1 << &item2);


    //setup the first table as a child of the first item
    for ( int row = 0; row < 40; ++row ) {
        item1.appendRow(QList<QStandardItem*>() << new QStandardItem(QLatin1String("row ") + QString::number(row)));
    }

    //setup the second table as a child of the second item
    for ( int row = 0; row < 10; ++row ) {
        item2.appendRow(QList<QStandardItem*>() << new QStandardItem(QLatin1String("row ") + QString::number(row)));
    }

    tableView.setModel(&model);

    //show the first 10 rows of the first table
    QModelIndex root = model.indexFromItem(&item1);
    tableView.setRootIndex(root);
    for (int i = 10; i != 40; ++i) {
        tableView.setRowHidden(i, true);
    }

    QCOMPARE(tableView.verticalHeader()->count(), 40);
    QCOMPARE(tableView.verticalHeader()->hiddenSectionCount(), 30);

    //show the first 10 rows of the second table
    tableView.setRootIndex(model.indexFromItem(&item2));

    QCOMPARE(tableView.verticalHeader()->count(), 10);
    QCOMPARE(tableView.verticalHeader()->hiddenSectionCount(), 0);
    QVERIFY(!tableView.verticalHeader()->isHidden());
}

void tst_QTableView::task240266_veryBigColumn()
{
    QTableView table;
    table.setFixedSize(500, 300); //just to make sure we have the 2 first columns visible
    QStandardItemModel model(1, 3);
    table.setModel(&model);
    table.setColumnWidth(0, 100); //normal column
    table.setColumnWidth(1, 100); //normal column
    table.setColumnWidth(2, 9000); //very big column
    table.show();
    QVERIFY(QTest::qWaitForWindowExposed(&table));

    //some styles change the scroll mode in their polish
    table.setHorizontalScrollMode(QAbstractItemView::ScrollPerItem);
    table.setVerticalScrollMode(QAbstractItemView::ScrollPerItem);

    QScrollBar *scroll = table.horizontalScrollBar();
    QCOMPARE(scroll->minimum(), 0);
    QCOMPARE(scroll->maximum(), model.columnCount() - 1);
    QCOMPARE(scroll->singleStep(), 1);

    //1 is not always a very correct value for pageStep. Ideally this should be dynamic.
    //Maybe something for Qt 5 ;-)
    QCOMPARE(scroll->pageStep(), 1);

}

void tst_QTableView::task248688_autoScrollNavigation()
{
    //we make sure that when navigating with the keyboard the view is correctly scrolled
    //to the current item
    QStandardItemModel model(16, 16);
    QTableView view;
    view.setModel(&model);

    view.hideColumn(8);
    view.hideRow(8);
    view.show();
    for (int r = 0; r < model.rowCount(); ++r) {
        if (view.isRowHidden(r))
            continue;
        for (int c = 0; c < model.columnCount(); ++c) {
            if (view.isColumnHidden(c))
                continue;
            QModelIndex index = model.index(r, c);
            view.setCurrentIndex(index);
            QVERIFY(view.viewport()->rect().contains(view.visualRect(index)));
        }
    }
}

#if QT_CONFIG(wheelevent)
void tst_QTableView::mouseWheel_data()
{
    QTest::addColumn<QAbstractItemView::ScrollMode>("scrollMode");
    QTest::addColumn<int>("delta");
    QTest::addColumn<int>("horizontalPositon");
    QTest::addColumn<int>("verticalPosition");

    QTest::newRow("scroll up per item")
            << QAbstractItemView::ScrollPerItem << 120
            << 10 - QApplication::wheelScrollLines() << 10 - QApplication::wheelScrollLines();
    QTest::newRow("scroll down per item")
            << QAbstractItemView::ScrollPerItem << -120
            << 10 + QApplication::wheelScrollLines() << 10 + QApplication::wheelScrollLines();
    QTest::newRow("scroll down per pixel")
            << QAbstractItemView::ScrollPerPixel << -120
            << 10 + QApplication::wheelScrollLines() * 91 << 10 + QApplication::wheelScrollLines() * 46;
}

void tst_QTableView::mouseWheel()
{
    QFETCH(QAbstractItemView::ScrollMode, scrollMode);
    QFETCH(int, delta);
    QFETCH(int, horizontalPositon);
    QFETCH(int, verticalPosition);

    QtTestTableModel model(100, 100);
    QWidget topLevel;
    QtTestTableView view(&topLevel);
    view.resize(500, 500);
    topLevel.show();

    QVERIFY(QTest::qWaitForWindowExposed(&topLevel));

    view.setModel(&model);

    for (int r = 0; r < 100; ++r)
        view.setRowHeight(r, 50);
    for (int c = 0; c < 100; ++c)
        view.setColumnWidth(c, 100);

    view.setHorizontalScrollMode(scrollMode);
    view.setVerticalScrollMode(scrollMode);
    view.horizontalScrollBar()->setValue(10);
    view.verticalScrollBar()->setValue(10);

    QPoint pos = view.viewport()->geometry().center();
    QWheelEvent verticalEvent(pos, view.mapToGlobal(pos), QPoint(), QPoint(0, delta),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QWheelEvent horizontalEvent(pos, view.mapToGlobal(pos), QPoint(), QPoint(delta, 0),
                                Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &horizontalEvent);
    QVERIFY(qAbs(view.horizontalScrollBar()->value() - horizontalPositon) < 15);
    QApplication::sendEvent(view.viewport(), &verticalEvent);
    QVERIFY(qAbs(view.verticalScrollBar()->value() - verticalPosition) < 15);
}
#endif // QT_CONFIG(wheelevent)

void tst_QTableView::addColumnWhileEditing()
{
    QTableView view;
    QStandardItemModel model(1, 10);
    view.setModel(&model);
    QModelIndex last = model.index(0,9);
    view.show();

    view.openPersistentEditor(last);
    view.scrollTo(last);

    //let's see if the editor is moved to the right location
    //after adding a column
    model.setColumnCount(model.columnCount() + 1);
    QPointer<QLineEdit> editor = view.findChild<QLineEdit*>();
    QVERIFY(editor);
    QCOMPARE(editor->geometry(), view.visualRect(last));

    //let's see if the editor is moved to the right location
    //after removing a column
    view.scrollTo(model.index(0, model.columnCount()-1));
    model.setColumnCount(model.columnCount() - 1);
    QVERIFY(editor);
    QCOMPARE(editor->geometry(), view.visualRect(last));
}

void tst_QTableView::task259308_scrollVerticalHeaderSwappedSections()
{
    QStandardItemModel model;
    model.setRowCount(50);
    model.setColumnCount(2);
    for (int row = 0; row < model.rowCount(); ++row)
        for (int col = 0; col < model.columnCount(); ++col) {
            const QModelIndex &idx = model.index(row, col);
            model.setData(idx, QVariant(row), Qt::EditRole);
        }

    QTableView tv;
    tv.setModel(&model);
    tv.show();
    tv.verticalHeader()->swapSections(0, model.rowCount() - 1);
    tv.setCurrentIndex(model.index(model.rowCount() - 1, 0));

    QVERIFY(QTest::qWaitForWindowExposed(&tv));
    QTest::keyClick(&tv, Qt::Key_PageUp);   // PageUp won't scroll when at top
    QTRY_COMPARE(tv.rowAt(0), tv.verticalHeader()->logicalIndex(0));

    int newRow = tv.rowAt(tv.viewport()->height());
    QTest::keyClick(&tv, Qt::Key_PageDown); // Scroll down and check current
    QTRY_COMPARE(tv.currentIndex().row(), newRow);

    tv.setCurrentIndex(model.index(0, 0));
    QTest::keyClick(&tv, Qt::Key_PageDown); // PageDown won't scroll when at the bottom
    QTRY_COMPARE(tv.rowAt(tv.viewport()->height() - 1), tv.verticalHeader()->logicalIndex(model.rowCount() - 1));
}

template <typename T>
struct ValueSaver {
    T &var, value;
    ValueSaver(T &v) : var(v), value(v) { }
    ~ValueSaver() { var = value; }
};

void tst_QTableView::task191545_dragSelectRows()
{
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"), Qt::CaseInsensitive))
        QSKIP("Wayland: This fails. Figure out why.");

    QStandardItemModel model(10, 10);
    QTableView table;
    table.setModel(&model);
    table.setSelectionBehavior(QAbstractItemView::SelectItems);
    table.setSelectionMode(QAbstractItemView::ExtendedSelection);
    table.setMinimumSize(1000, 400);
    table.show();
    QVERIFY(QTest::qWaitForWindowActive(&table));

    ValueSaver<Qt::KeyboardModifiers> saver(QApplicationPrivate::modifier_buttons);
    QApplicationPrivate::modifier_buttons = Qt::ControlModifier;

    {
        QRect cellRect = table.visualRect(model.index(3, 0));
        QHeaderView *vHeader = table.verticalHeader();
        QWidget *vHeaderVp = vHeader->viewport();
        QPoint rowPos(cellRect.center());
        QMouseEvent rowPressEvent(QEvent::MouseButtonPress, rowPos, rowPos, vHeaderVp->mapToGlobal(rowPos),
                                  Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
        QCoreApplication::sendEvent(vHeaderVp, &rowPressEvent);

        for (int i = 0; i < 4; ++i) {
            rowPos.setY(rowPos.y() + cellRect.height());
            QMouseEvent moveEvent(QEvent::MouseMove, rowPos, rowPos, vHeaderVp->mapToGlobal(rowPos),
                                  Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
            QCoreApplication::sendEvent(vHeaderVp, &moveEvent);
        }
        QMouseEvent rowReleaseEvent(QEvent::MouseButtonRelease, rowPos, vHeaderVp->mapToGlobal(rowPos),
                                    Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
        QCoreApplication::sendEvent(vHeaderVp, &rowReleaseEvent);

        for (int i = 0; i < 4; ++i) {
            QModelIndex index = model.index(3 + i, 0, table.rootIndex());
            QVERIFY(vHeader->selectionModel()->selectedRows().contains(index));
        }
    }

    {
        QRect cellRect = table.visualRect(model.index(0, 3));
        QHeaderView *hHeader = table.horizontalHeader();
        QWidget *hHeaderVp = hHeader->viewport();
        QPoint colPos((cellRect.left() + cellRect.right()) / 2, 5);
        QMouseEvent colPressEvent(QEvent::MouseButtonPress, colPos, hHeaderVp->mapToGlobal(colPos),
                                  Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
        QCoreApplication::sendEvent(hHeaderVp, &colPressEvent);

        for (int i = 0; i < 4; ++i) {
            colPos.setX(colPos.x() + cellRect.width());
            QMouseEvent moveEvent(QEvent::MouseMove, colPos, hHeaderVp->mapToGlobal(colPos),
                                  Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
            QCoreApplication::sendEvent(hHeaderVp, &moveEvent);
        }
        QMouseEvent colReleaseEvent(QEvent::MouseButtonRelease, colPos, hHeaderVp->mapToGlobal(colPos),
                                    Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
        QCoreApplication::sendEvent(hHeaderVp, &colReleaseEvent);

        for (int i = 0; i < 4; ++i) {
            QModelIndex index = model.index(0, 3 + i, table.rootIndex());
            QVERIFY(hHeader->selectionModel()->selectedColumns().contains(index));
        }
    }

    {
        QRect cellRect = table.visualRect(model.index(2, 2));
        QWidget *tableVp = table.viewport();
        QPoint cellPos = cellRect.center();
        QMouseEvent cellPressEvent(QEvent::MouseButtonPress, cellPos, tableVp->mapToGlobal(cellPos),
                                   Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
        QCoreApplication::sendEvent(tableVp, &cellPressEvent);

        for (int i = 0; i < 6; ++i) {
            cellPos.setX(cellPos.x() + cellRect.width());
            cellPos.setY(cellPos.y() + cellRect.height());
            QMouseEvent moveEvent(QEvent::MouseMove, cellPos, tableVp->mapToGlobal(cellPos),
                                  Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
            QCoreApplication::sendEvent(tableVp, &moveEvent);
        }
        QMouseEvent cellReleaseEvent(QEvent::MouseButtonRelease, cellPos, tableVp->mapToGlobal(cellPos),
                                     Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
        QCoreApplication::sendEvent(tableVp, &cellReleaseEvent);

        for (int i = 0; i < 6; ++i) {
            for (int j = 0; j < 6; ++j) {
                QModelIndex index = model.index(2 + i, 2 + j, table.rootIndex());
                QVERIFY(table.selectionModel()->isSelected(index));
            }
        }
    }

    {
        QRect cellRect = table.visualRect(model.index(3, 3));
        QWidget *tableVp = table.viewport();
        QPoint cellPos = cellRect.center();
        QMouseEvent cellPressEvent(QEvent::MouseButtonPress, cellPos, tableVp->mapToGlobal(cellPos),
                                   Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
        QCoreApplication::sendEvent(tableVp, &cellPressEvent);

        for (int i = 0; i < 6; ++i) {
            // cellPos might have been updated by scrolling, so refresh
            cellPos = table.visualRect(model.index(3+i, 3+i)).center();
            cellPos.setX(cellPos.x() + cellRect.width());
            cellPos.setY(cellPos.y() + cellRect.height());
            QMouseEvent moveEvent(QEvent::MouseMove, cellPos, tableVp->mapToGlobal(cellPos),
                                  Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
            QCoreApplication::sendEvent(tableVp, &moveEvent);
        }
        QMouseEvent cellReleaseEvent(QEvent::MouseButtonRelease, cellPos, tableVp->mapToGlobal(cellPos),
                                     Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
        QCoreApplication::sendEvent(tableVp, &cellReleaseEvent);

        QTest::qWait(200);
        for (int i = 0; i < 6; ++i) {
            for (int j = 0; j < 6; ++j) {
                QModelIndex index = model.index(3 + i, 3 + j, table.rootIndex());
                QVERIFY(!table.selectionModel()->isSelected(index));
            }
        }
    }
}

void tst_QTableView::task234926_setHeaderSorting()
{
    QStringListModel model;
    QSortFilterProxyModel sfpm; // default QStandardItemModel does not support 'unsorted' state
    sfpm.setSourceModel(&model);
    const QStringList data({"orange", "apple", "banana", "lemon", "pumpkin"});
    QStringList sortedDataA = data;
    QStringList sortedDataD = data;
    std::sort(sortedDataA.begin(), sortedDataA.end());
    std::sort(sortedDataD.begin(), sortedDataD.end(), std::greater<QString>());
    model.setStringList(data);
    QTableView view;
    view.setModel(&sfpm);

    QTRY_COMPARE(model.stringList(), data);
    view.setSortingEnabled(true);
    view.sortByColumn(0, Qt::AscendingOrder);
    for (int i = 0; i < sortedDataA.size(); ++i)
        QCOMPARE(view.model()->data(view.model()->index(i, 0)).toString(), sortedDataA.at(i));

    view.horizontalHeader()->setSortIndicator(0, Qt::DescendingOrder);
    for (int i = 0; i < sortedDataD.size(); ++i)
        QCOMPARE(view.model()->data(view.model()->index(i, 0)).toString(), sortedDataD.at(i));

    QHeaderView *h = new QHeaderView(Qt::Horizontal);
    h->setModel(&model);
    view.setHorizontalHeader(h);
    h->setSortIndicator(0, Qt::AscendingOrder);
    for (int i = 0; i < sortedDataA.size(); ++i)
        QCOMPARE(view.model()->data(view.model()->index(i, 0)).toString(), sortedDataA.at(i));

    h->setSortIndicator(0, Qt::DescendingOrder);
    for (int i = 0; i < sortedDataD.size(); ++i)
        QCOMPARE(view.model()->data(view.model()->index(i, 0)).toString(), sortedDataD.at(i));

    view.sortByColumn(-1, Qt::AscendingOrder);
    QCOMPARE(view.horizontalHeader()->sortIndicatorSection(), -1);
    for (int i = 0; i < data.size(); ++i)
        QCOMPARE(view.model()->data(view.model()->index(i, 0)).toString(), data.at(i));
}

void tst_QTableView::taskQTBUG_5062_spansInconsistency()
{
    const int nRows = 5;
    const int nColumns = 5;

    QtTestTableModel model(nRows, nColumns);
    QtTestTableView view;
    view.setModel(&model);

    for (int i = 0; i < nRows; ++i)
       view.setSpan(i, 0, 1, nColumns);
    view.setSpan(2, 0, 1, 1);
    view.setSpan(3, 0, 1, 1);

    VERIFY_SPANS_CONSISTENCY(&view);
}

void tst_QTableView::taskQTBUG_4516_clickOnRichTextLabel()
{
    QTableView view;
    QStandardItemModel model(5,5);
    view.setModel(&model);
    QLabel label("rich text");
    label.setTextFormat(Qt::RichText);
    view.setIndexWidget(model.index(1,1), &label);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    view.setCurrentIndex(model.index(0,0));
    QCOMPARE(view.currentIndex(), model.index(0,0));

    QTest::mouseClick(&label, Qt::LeftButton);
    QCOMPARE(view.currentIndex(), model.index(1,1));
}


void tst_QTableView::changeHeaderData()
{
    QTableView view;
    QStandardItemModel model(5,5);
    view.setModel(&model);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QString text = "long long long text";
    const int textWidth = view.verticalHeader()->fontMetrics().horizontalAdvance(text);
    QVERIFY(view.verticalHeader()->width() < textWidth);

    model.setHeaderData(2, Qt::Vertical, text);

    QTRY_VERIFY(view.verticalHeader()->width() > textWidth);
}

#if QT_CONFIG(wheelevent)
void tst_QTableView::taskQTBUG_5237_wheelEventOnHeader()
{
    QTableView view;
    QStandardItemModel model(500,5);
    view.setModel(&model);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    int sbValueBefore = view.verticalScrollBar()->value();
    QHeaderView *header = view.verticalHeader();
    QTest::mouseMove(header);
    QPoint pos = header->geometry().center();
    QWheelEvent wheelEvent(pos, header->viewport()->mapToGlobal(pos), QPoint(), QPoint(0, -720),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(header->viewport(), &wheelEvent);
    int sbValueAfter = view.verticalScrollBar()->value();
    QVERIFY(sbValueBefore != sbValueAfter);
}
#endif

class TestTableView : public QTableView
{
    Q_OBJECT
public:
    TestTableView(QWidget *parent = nullptr) : QTableView(parent)
    {
        connect(this, &QTableView::entered, this, &TestTableView::openPersistentEditor);
    }
public slots:
    void onDataChanged()
    {
        for (int i = 0; i < model()->rowCount(); i++) {
            setRowHidden(i, model()->data(model()->index(i, 0)).toBool());
        }
    }
};


void tst_QTableView::taskQTBUG_8585_crashForNoGoodReason()
{
    QStandardItemModel model;
    model.insertColumn(0, QModelIndex());
    for (int i = 0; i < 20; i++)
        model.insertRow(i);

    TestTableView w;
    w.setMouseTracking(true);
    w.setModel(&model);
    connect(&model, &QStandardItemModel::dataChanged, &w, &TestTableView::onDataChanged);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    for (int i = 0; i < 10; i++)
    {
        QTest::mouseMove(w.viewport(), QPoint(50, 20));
        w.model()->setData(w.indexAt(QPoint(50, 20)), true);
        QTest::mouseMove(w.viewport(), QPoint(50, 25));
    }
}

class TableView7774 : public QTableView
{
public:
    using QTableView::visualRegionForSelection;
};

void tst_QTableView::taskQTBUG_7774_RtoLVisualRegionForSelection()
{
    TableView7774 view;
    QStandardItemModel model(5,5);
    view.setModel(&model);
    view.setLayoutDirection(Qt::RightToLeft);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QItemSelectionRange range(model.index(2, 0), model.index(2, model.columnCount() - 1));
    QItemSelection selection;
    selection << range;
    QRegion region = view.visualRegionForSelection(selection);
    QVERIFY(!region.isEmpty());
    QCOMPARE(region.begin()[0], view.visualRect(range.topLeft()) | view.visualRect(range.bottomRight()));
}

void tst_QTableView::taskQTBUG_8777_scrollToSpans()
{
    QTableWidget table(75,5);
    for (int i=0; i<50; i++)
        table.setSpan(2+i, 0, 1, 5);
    table.setCurrentCell(0,2);
    table.show();

    for (int i = 0; i < 45; ++i)
        QTest::keyClick(&table, Qt::Key_Down);

    QVERIFY(table.verticalScrollBar()->value() > 10);
}

void tst_QTableView::taskQTBUG_10169_sizeHintForRow()
{
    QtTestTableView tableView;
    QStandardItemModel model(1, 3);
    model.setData(model.index(0, 0), "Word wrapping text goes here.");
    tableView.setModel(&model);
    tableView.verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    const int orderedHeight = tableView.sizeHintForRow(0);
    tableView.horizontalHeader()->moveSection(2, 0);
    const int reorderedHeight = tableView.sizeHintForRow(0);

    //the order of the columns shouldn't matter.
    QCOMPARE(orderedHeight, reorderedHeight);
}

void tst_QTableView::viewOptions()
{
    QtTestTableView view;
    QStyleOptionViewItem options;
    view.initViewItemOption(&options);
    QVERIFY(options.showDecorationSelected);
}

void tst_QTableView::taskQTBUG_30653_doItemsLayout()
{
    QWidget topLevel;
    QtTestTableView view(&topLevel);

    QtTestTableModel model(5, 5);
    view.setModel(&model);

    QtTestItemDelegate delegate;
    delegate.hint = QSize(50, 50);
    view.setItemDelegate(&delegate);

    view.resizeRowsToContents();
    view.resizeColumnsToContents();

    // show two and half rows/cols
    int extraWidth = view.verticalHeader()->sizeHint().width() + view.verticalScrollBar()->sizeHint().width();
    int extraHeight = view.horizontalHeader()->sizeHint().height() + view.horizontalScrollBar()->sizeHint().height();
    view.resize(125 + extraWidth, 125 + extraHeight);

    topLevel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&topLevel));

    // the offset after scrollToBottom() and doItemsLayout() should not differ
    // as the view content should stay aligned to the last section
    view.scrollToBottom();
    int scrollToBottomOffset = view.verticalHeader()->offset();
    view.doItemsLayout();
    int doItemsLayoutOffset = view.verticalHeader()->offset();

    QCOMPARE(scrollToBottomOffset, doItemsLayoutOffset);
}

void tst_QTableView::taskQTBUG_7232_AllowUserToControlSingleStep()
{
    // When we set the scrollMode to ScrollPerPixel it will adjust the scrollbars singleStep automatically
    // Setting a singlestep on a scrollbar should however imply that the user takes control (and it is not changed by geometry updates).
    // Setting a singlestep to -1 return to an automatic control of the singleStep.
    QTableView t;
    t.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    t.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    QStandardItemModel model(200, 200);
    t.setModel(&model);
    t.show();
    QVERIFY(QTest::qWaitForWindowExposed(&t));
    t.setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    t.setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    t.setGeometry(200, 200, 200, 200);
    int vStep1 = t.verticalScrollBar()->singleStep();
    int hStep1 = t.horizontalScrollBar()->singleStep();
    QVERIFY(vStep1 > 1);
    QVERIFY(hStep1 > 1);

    t.verticalScrollBar()->setSingleStep(1);
    t.setGeometry(300, 300, 300, 300);
    QCOMPARE(t.verticalScrollBar()->singleStep(), 1);

    t.horizontalScrollBar()->setSingleStep(1);
    t.setGeometry(400, 400, 400, 400);
    QCOMPARE(t.horizontalScrollBar()->singleStep(), 1);

    t.setGeometry(200, 200, 200, 200);
    t.verticalScrollBar()->setSingleStep(-1);
    t.horizontalScrollBar()->setSingleStep(-1);
    QCOMPARE(vStep1, t.verticalScrollBar()->singleStep());
    QCOMPARE(hStep1, t.horizontalScrollBar()->singleStep());
}

void tst_QTableView::taskQTBUG_50171_selectRowAfterSwapColumns()
{
    {
        QtTestTableView tableView;
        QtTestTableModel model(2, 3);
        tableView.setModel(&model);

        tableView.horizontalHeader()->swapSections(1, 2);
        tableView.horizontalHeader()->hideSection(0);
        tableView.selectRow(1);

        QItemSelectionModel* tableSelectionModel = tableView.selectionModel();
        QCOMPARE(tableSelectionModel->isRowSelected(1, QModelIndex()), true);
        QCOMPARE(tableSelectionModel->isSelected(tableView.model()->index(0, 0)), false);
        QCOMPARE(tableSelectionModel->isSelected(tableView.model()->index(0, 1)), false);
        QCOMPARE(tableSelectionModel->isSelected(tableView.model()->index(0, 2)), false);
    }

    {
        QtTestTableView tableView;
        QtTestTableModel model(3, 2);
        tableView.setModel(&model);

        tableView.verticalHeader()->swapSections(1, 2);
        tableView.verticalHeader()->hideSection(0);
        tableView.selectColumn(1);

        QItemSelectionModel* sModel = tableView.selectionModel();
        QCOMPARE(sModel->isColumnSelected(1, QModelIndex()), true);
        QCOMPARE(sModel->isSelected(tableView.model()->index(0, 0)), false);
        QCOMPARE(sModel->isSelected(tableView.model()->index(1, 0)), false);
        QCOMPARE(sModel->isSelected(tableView.model()->index(2, 0)), false);
    }
}

class DeselectTableWidget : public QTableWidget
{
public:
    using QTableWidget::QTableWidget;
    QItemSelectionModel::SelectionFlags selectionCommand(const QModelIndex &,
                                                         const QEvent * = nullptr) const override
    {
        return QItemSelectionModel::Toggle;
    }
};

void tst_QTableView::deselectRow()
{
    DeselectTableWidget tw(20, 20);
    tw.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tw));
    tw.hideColumn(0);
    QVERIFY(tw.isColumnHidden(0));
    tw.selectRow(1);
    QVERIFY(tw.selectionModel()->isRowSelected(1, QModelIndex()));
    tw.selectRow(1);
    // QTBUG-79092 - deselection was not possible when column 0 was hidden
    QVERIFY(!tw.selectionModel()->isRowSelected(1, QModelIndex()));
}

class QTableViewSelectCells : public QTableView
{
public:
    QItemSelectionModel::SelectionFlags selectionCommand(const QModelIndex &index,
                                                         const QEvent *) const override
    {
        return QTableView::selectionCommand(index, shiftPressed ? &mouseEvent : nullptr);
    }
    QMouseEvent mouseEvent = QMouseEvent(QEvent::MouseButtonPress, QPointF(), QPointF(),
                                         Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    bool shiftPressed = false;
};

void tst_QTableView::selectRowsAndCells()
{
    const auto checkRows = [](const QModelIndexList &mil)
    {
        QCOMPARE(mil.size(), 3);
        for (const auto &mi : mil)
            QVERIFY(mi.row() >= 1 && mi.row() <= 3);
    };
    QTableViewSelectCells tw;
    QtTestTableModel model(5, 1);
    tw.setSelectionBehavior(QAbstractItemView::SelectRows);
    tw.setSelectionMode(QAbstractItemView::ExtendedSelection);
    tw.setModel(&model);
    tw.show();

    tw.selectRow(1);
    tw.shiftPressed = true;
    tw.selectRow(2);
    tw.shiftPressed = false;
    QTest::mouseClick(tw.viewport(), Qt::LeftButton, Qt::ShiftModifier, tw.visualRect(model.index(3, 0)).center());
    checkRows(tw.selectionModel()->selectedRows());

    tw.clearSelection();
    QTest::mouseClick(tw.viewport(), Qt::LeftButton, Qt::NoModifier, tw.visualRect(model.index(3, 0)).center());
    tw.shiftPressed = true;
    tw.selectRow(1);
    checkRows(tw.selectionModel()->selectedRows());
}

void tst_QTableView::selectColumnsAndCells()
{
    const auto checkColumns = [](const QModelIndexList &mil)
    {
        QCOMPARE(mil.size(), 3);
        for (const auto &mi : mil)
            QVERIFY(mi.column() >= 1 && mi.column() <= 3);
    };
    QTableViewSelectCells tw;
    QtTestTableModel model(1, 5);
    tw.setSelectionBehavior(QAbstractItemView::SelectColumns);
    tw.setSelectionMode(QAbstractItemView::ExtendedSelection);
    tw.setModel(&model);
    tw.show();

    tw.selectColumn(1);
    tw.shiftPressed = true;
    tw.selectColumn(2);
    tw.shiftPressed = false;
    QTest::mouseClick(tw.viewport(), Qt::LeftButton, Qt::ShiftModifier, tw.visualRect(model.index(0, 3)).center());
    checkColumns(tw.selectionModel()->selectedColumns());

    tw.clearSelection();
    QTest::mouseClick(tw.viewport(), Qt::LeftButton, Qt::NoModifier, tw.visualRect(model.index(0, 3)).center());
    tw.shiftPressed = true;
    tw.selectColumn(1);
    checkColumns(tw.selectionModel()->selectedColumns());
}

void tst_QTableView::selectWithHeader_data()
{
    QTest::addColumn<Qt::Orientation>("orientation");

    QTest::addRow("horizontal") << Qt::Horizontal;
    QTest::addRow("vertical") << Qt::Vertical;
}

void tst_QTableView::selectWithHeader()
{
    QFETCH(Qt::Orientation, orientation);

    QTableWidget view(10, 10);
    view.resize(200, 100);
    view.show();

    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QHeaderView *header = nullptr;
    QPoint clickPos;
    QModelIndex lastIndex;

    switch (orientation) {
    case Qt::Horizontal:
        header = view.horizontalHeader();
        clickPos.rx() = header->sectionPosition(0) + header->sectionSize(0) / 2;
        clickPos.ry() = header->height() / 2;
        lastIndex = view.model()->index(9, 0);
        break;
    case Qt::Vertical:
        header = view.verticalHeader();
        clickPos.rx() = header->width() / 2;
        clickPos.ry() = header->sectionPosition(0) + header->sectionSize(0) / 2;
        lastIndex = view.model()->index(0, 9);
        break;
    }

    const auto isSelected = [&]{
        return orientation == Qt::Horizontal
             ? view.selectionModel()->isColumnSelected(0)
             : view.selectionModel()->isRowSelected(0);
    };

    QTest::mouseClick(header->viewport(), Qt::LeftButton, {}, clickPos);
    QVERIFY(isSelected());
    QTest::mouseClick(header->viewport(), Qt::LeftButton, Qt::ControlModifier, clickPos);
    QVERIFY(!isSelected());
    QTest::mouseClick(header->viewport(), Qt::LeftButton, {}, clickPos);
    QVERIFY(isSelected());
    view.scrollTo(lastIndex);
    QTest::mouseClick(header->viewport(), Qt::LeftButton, Qt::ControlModifier, clickPos);
    QVERIFY(!isSelected());
}

void tst_QTableView::resetDefaultSectionSize()
{
    // Create a table and change its default section size and then reset it.
    // This should be a no op so clicking on row 1 should select row 1 and not row
    // 0 as previously. QTBUG-116013
    QTableWidget view(10, 10);
    view.resize(300, 300);
    view.verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    view.verticalHeader()->setDefaultSectionSize(120);
    view.verticalHeader()->resetDefaultSectionSize();
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QEXPECT_FAIL("", "Reverted fix for QTBUG-116013 due to QTBUG-122109", Continue);
    QCOMPARE(view.verticalHeader()->logicalIndexAt(9, 45), 1);
}

// This has nothing to do with QTableView, but it's convenient to reuse the QtTestTableModel
#if QT_CONFIG(textmarkdownwriter)

// #define DEBUG_WRITE_OUTPUT

void tst_QTableView::markdownWriter()
{
    QtTestTableModel model(2, 3);
    QString md;
    {
        QTextStream stream(&md);
        QTextMarkdownWriter writer(stream, QTextDocument::MarkdownDialectGitHub);
        writer.writeTable(&model);
    }

#ifdef DEBUG_WRITE_OUTPUT
    {
        QFile out("/tmp/table.md");
        out.open(QFile::WriteOnly);
        out.write(md.toUtf8());
        out.close();
    }
#endif

    QCOMPARE(md, QString::fromLatin1("|1      |2      |3      |\n|-------|-------|-------|\n|[0,0,0]|[0,1,0]|[0,2,0]|\n|[1,0,0]|[1,1,0]|[1,2,0]|\n"));
}
#endif

void tst_QTableView::rowsInVerticalHeader()
{
    QtTestTableModel model(0, 2);
    QTableView view;
    view.setModel(&model);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *verticalHeader = view.verticalHeader();
    QCOMPARE(verticalHeader->count(), 0);
    model.insertRows(2);
    QCOMPARE(verticalHeader->count(), 2);
}

QTEST_MAIN(tst_QTableView)
#include "tst_qtableview.moc"
