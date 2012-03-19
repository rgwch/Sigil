/************************************************************************
**
**  Copyright (C) 2009, 2010, 2011  Strahinja Markovic  <strahinja.markovic@gmail.com>
**  Copyright (C) 2011  John Schember <john@nachtimwald.com>
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  Sigil is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Sigil.  If not, see <http://www.gnu.org/licenses/>.
**
*************************************************************************/

#include <pcre.h>

#include <QtGui/QKeyEvent>
#include <QtGui/QLineEdit>
#include <QtGui/QMessageBox>

#include "MainUI/FindReplace.h"
#include "Misc/SettingsStore.h"
#include "Misc/SleepFunctions.h"

static const QString SETTINGS_GROUP = "find_replace";
static const int MAXIMUM_SELECTED_TEXT_LIMIT = 100;
static const int MAX_HISTORY_COUNT = 15;

FindReplace::FindReplace( MainWindow &main_window )
    : QWidget( &main_window ),
      m_MainWindow( main_window )
{
    ui.setupUi( this );

    ExtendUI();
    ConnectSignalsToSlots();

    ReadSettings();
}


// Destructor
FindReplace::~FindReplace()
{
    WriteSettings();
}


void FindReplace::SetUpFindText()
{
    Searchable* searchable = GetAvailableSearchable();

    if ( searchable )
    {
        QString selected_text = searchable->GetSelectedText();

        // We want to make the text selected in the editor
        // as the default search text, but only if it's not "too long"
        if ( !selected_text.isEmpty() &&
             selected_text.length() < MAXIMUM_SELECTED_TEXT_LIMIT )
        {
            ui.cbFind->setEditText( selected_text );
            ui.cbFind->insertItem( 0, selected_text );
        }
    }

    // Find text should be selected by default
    ui.cbFind->lineEdit()->selectAll();
    ui.cbFind->lineEdit()->setFocus( Qt::ShortcutFocusReason );
}


void FindReplace::close()
{
    clearMessage();
    QWidget::close();
}


void FindReplace::show()
{
    clearMessage();
    QWidget::show();
}


void FindReplace::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
    }
}


void FindReplace::ShowMessage( const QString &message )
{
    QFont f = ui.message->font();
    f.setBold(true);
    ui.message->setFont(f);

    ui.message->setText(message);
    m_timer.start(15000);
}


void FindReplace::Find()
{
    if ( GetSearchDirection() == SearchDirection_Up )
    {
        FindPrevious();
    }
    else
    {
        FindNext();
    }
}


void FindReplace::FindNext()
{
    FindText( Searchable::Direction_Down );
}


void FindReplace::FindPrevious()
{
    FindText( Searchable::Direction_Up );
}


// Counts the number of occurrences of the user's
// term in the document.
void FindReplace::Count()
{
    clearMessage();

    if ( !IsValidFindText() )
    {
        return;
    }

    SetCodeViewIfNeeded();

    int count = 0;

    if ( GetLookWhere() == LookWhere_CurrentFile )
    {
        Searchable *searchable = GetAvailableSearchable();

        if ( !searchable )
        {
            return;
        }

        count = searchable->Count( GetSearchRegex(), GetSearchMode() == SearchMode_SpellCheck );
    }
    else
    {
        count = CountInFiles();
    }

    if ( count == 0 )
    {
        CannotFindSearchTerm();
    }
    else
    {
        QString message = tr( "%1 matches found", 0, count );
        ShowMessage( message.arg( count ) );
    }

    UpdatePreviousFindStrings();
}


void FindReplace::Replace()
{
    if ( GetSearchDirection() == SearchDirection_Up )
    {
        ReplacePrevious();
    }
    else
    {
        ReplaceNext();
    }
}

void FindReplace::ReplaceNext()
{
    ReplaceText( Searchable::Direction_Down );
}


void FindReplace::ReplacePrevious()
{
    ReplaceText( Searchable::Direction_Up );
}


// Replaces the user's search term with the user's
// replacement text in the entire document. 
void FindReplace::ReplaceAll()
{
    m_MainWindow.GetCurrentContentTab().SaveTabContent();

    clearMessage();

    if ( !IsValidFindText() )
    {
        return;
    }

    SetCodeViewIfNeeded( true );

    int count = 0;

    if ( GetLookWhere() == LookWhere_CurrentFile )
    {
        Searchable *searchable = GetAvailableSearchable();

        if ( !searchable )
        {
            return;
        }
        count = searchable->ReplaceAll( GetSearchRegex(), ui.cbReplace->lineEdit()->text(), GetSearchMode() == SearchMode_SpellCheck );
    }
    else 
    {
        count = ReplaceInAllFiles();
    }

    if ( count == 0 )
    {
        ShowMessage( tr( "No replacements made" ) );
    }
    else
    {
        QString message = tr( "%1 replacements made", 0, count );
        ShowMessage( message.arg( count ) );
    }

    if ( count > 0 )
    {
        // Signal that the contents have changed and update the view
        m_MainWindow.GetCurrentBook()->SetModified( true );
        m_MainWindow.GetCurrentContentTab().ContentChangedExternally();
    }


    UpdatePreviousFindStrings();
    UpdatePreviousReplaceStrings();
}


void FindReplace::clearMessage()
{
    ui.message->clear();
}

void FindReplace::expireMessage()
{
    QFont f = ui.message->font();
    f.setBold(false);
    ui.message->setFont(f);

    m_timer.stop();
}

// Starts the search for the user's term.
void FindReplace::FindText( Searchable::Direction direction )
{
    clearMessage();

    if ( !IsValidFindText() )
    {
        return;
    }

    SetCodeViewIfNeeded();

    bool found = false;

    if ( GetLookWhere() == LookWhere_CurrentFile )
    {
        Searchable *searchable = GetAvailableSearchable();

        if ( !searchable )
        {
            return;
        }

        found = searchable->FindNext( GetSearchRegex(), direction, GetSearchMode() == SearchMode_SpellCheck );

    }
    else 
    {
        found = FindInAllFiles( direction );
    }

    if ( found )
    {
        clearMessage();
    }
    else
    {
        CannotFindSearchTerm();
    }

    UpdatePreviousFindStrings();
}


// Replaces the user's search term with the user's
// replacement text if a match is selected. If it's not,
// calls Find in the direction specified so it becomes selected.
void FindReplace::ReplaceText( Searchable::Direction direction )
{
    clearMessage();

    if ( !IsValidFindText() )
    {
        return;
    }

    SetCodeViewIfNeeded( true );

    Searchable *searchable = GetAvailableSearchable();

    if ( searchable )
    {
        // If we have the matching text selected, replace it
        // This will not do anything if matching text is not selected.
        searchable->ReplaceSelected( GetSearchRegex(), ui.cbReplace->lineEdit()->text(), direction, GetSearchMode() == SearchMode_SpellCheck );
    }

    if ( direction == Searchable::Direction_Up)
    {
        FindPrevious();
    }
    else
    {
        FindNext();
    }

    UpdatePreviousFindStrings();
    UpdatePreviousReplaceStrings();
}


void FindReplace::SetCodeViewIfNeeded( bool force )
{
    if ( force ||
            ( ( GetLookWhere() == FindReplace::LookWhere_AllHTMLFiles || 
                    GetSearchMode() == FindReplace::SearchMode_SpellCheck ||
                    GetLookWhere() == FindReplace::LookWhere_SelectedHTMLFiles ) &&
                m_MainWindow.GetViewState() == MainWindow::ViewState_BookView ) )
    {
        // Force change to Code View
        m_MainWindow.AnyCodeView();
    }
}


// Displays a message to the user informing him
// that his last search term could not be found.
void FindReplace::CannotFindSearchTerm()
{
    ShowMessage( tr( "No matches found" ) );
}


// Constructs a searching regex from the selected
// options and fields and then returns it.
QString FindReplace::GetSearchRegex()
{
    QString search( ui.cbFind->currentText() );

    // Search type
    if ( GetSearchMode() == SearchMode_Normal || GetSearchMode() == SearchMode_Case_Sensitive )
    {
        search = QRegExp::escape(search);

        if ( GetSearchMode() == SearchMode_Normal ) {
            search = "(?i)" + search;
        }
    }
    else if ( GetSearchMode() == SearchMode_RegexDotall )
    {
            search = "(?s)" + search;
    }
	else if ( GetSearchMode() == SearchMode_SpellCheck && ui.cbFind->currentText().isEmpty() )
    {
        search = ".*";
    }

    return search;
}

bool FindReplace::IsCurrentFileInHTMLSelection()
{
    bool found = false;

    QList <Resource *> resources = GetHTMLFiles();
    Resource *current_resource = GetCurrentResource();
    HTMLResource *current_html_resource = qobject_cast< HTMLResource *>( current_resource );

    if ( current_html_resource )
    {
        foreach ( Resource *resource, resources )
        {
            if ( resource->Filename() == current_html_resource->Filename() )
            {
                found = true;
                break;
            }
        }
    }
    return found;
}

// Returns all html resources or only those selected in Book Browser
QList <Resource *> FindReplace::GetHTMLFiles()
{
    // For now, this must hold
    Q_ASSERT( GetLookWhere() == LookWhere_AllHTMLFiles || GetLookWhere() == LookWhere_SelectedHTMLFiles );
    QList <Resource *> resources;

    if ( GetLookWhere() == LookWhere_AllHTMLFiles )
    {
        resources = m_MainWindow.GetAllHTMLResources();
    }
    else 
    {
        resources = m_MainWindow.GetValidSelectedHTMLResources();
    }

    return resources;
}

int FindReplace::CountInFiles()
{
    // For now, this must hold
    Q_ASSERT( GetLookWhere() == LookWhere_AllHTMLFiles || GetLookWhere() == LookWhere_SelectedHTMLFiles );

    m_MainWindow.GetCurrentContentTab().SaveTabContent();

    return SearchOperations::CountInFiles(
            GetSearchRegex(),
            GetHTMLFiles(),
            SearchOperations::CodeViewSearch, 
            GetSearchMode() == SearchMode_SpellCheck );
}


int FindReplace::ReplaceInAllFiles()
{
    // For now, this must hold
    Q_ASSERT( GetLookWhere() == LookWhere_AllHTMLFiles || GetLookWhere() == LookWhere_SelectedHTMLFiles );

    m_MainWindow.GetCurrentContentTab().SaveTabContent();

    int count = SearchOperations::ReplaceInAllFIles(
            GetSearchRegex(),
            ui.cbReplace->lineEdit()->text(),
            GetHTMLFiles(),
            SearchOperations::CodeViewSearch,
            GetSearchMode() == SearchMode_SpellCheck );

    // Update the content displayed in the current tab.
    m_MainWindow.GetCurrentContentTab().LoadTabContent();

    return count;
}


bool FindReplace::FindInAllFiles( Searchable::Direction direction )
{
    Searchable *searchable = 0;

    bool found = false;
    if ( IsCurrentFileInHTMLSelection() )
    {
        searchable = GetAvailableSearchable();
        if ( searchable )
        {
            found = searchable->FindNext( GetSearchRegex(), direction, GetSearchMode() == SearchMode_SpellCheck, false, false);
        }
    }

    if ( !found )
    {
        // TODO: make this handle all types of files
        Resource *containing_resource = GetNextContainingHTMLResource( direction );

        if ( containing_resource )
        {
            // Save/restore selection since opening tabs changes selection 
            if ( GetLookWhere() == LookWhere_SelectedHTMLFiles )
            {
                m_MainWindow.SaveBrowserSelection();
            }

            m_MainWindow.OpenResource( *containing_resource, MainWindow::ViewState_CodeView );

            while ( !m_MainWindow.GetCurrentContentTab().IsLoadingFinished() )
            {
                // Make sure Qt processes events, signals and calls slots
                qApp->processEvents();
                SleepFunctions::msleep( 100 );
            }

            if ( GetLookWhere() == LookWhere_SelectedHTMLFiles )
            {
                m_MainWindow.RestoreBrowserSelection();
            }

            searchable = GetAvailableSearchable();
            if ( searchable )
            {
                found = searchable->FindNext( GetSearchRegex(), direction, GetSearchMode() == SearchMode_SpellCheck, true, false );
            }
        }
        else
        {
            if ( searchable )
            {
                // Check the part of the original file above the cursor
                found = searchable->FindNext( GetSearchRegex(), direction, GetSearchMode() == SearchMode_SpellCheck, false, false );
            }
        }
    }

    return found;
}

HTMLResource* FindReplace::GetNextContainingHTMLResource( Searchable::Direction direction )
{
    Resource* current_resource = GetCurrentResource();
    HTMLResource *starting_html_resource = qobject_cast< HTMLResource *> ( current_resource );

    if ( !starting_html_resource )
    {
        QList<Resource *> resources = GetHTMLFiles();
        if ( resources.isEmpty() )
        {
            return NULL;
        }
        if ( direction == Searchable::Direction_Up )
        {
            starting_html_resource = qobject_cast< HTMLResource *>( resources.first() );
        }
        else
        {
            starting_html_resource = qobject_cast< HTMLResource *>( resources.last() );
        }
    }

    HTMLResource *next_html_resource = starting_html_resource;

    bool passed_starting_html_resource = false;

    while ( !passed_starting_html_resource || ( next_html_resource != starting_html_resource ) )
    {
        next_html_resource = GetNextHTMLResource( next_html_resource, direction );
        if ( next_html_resource == starting_html_resource )
        {
             passed_starting_html_resource = true ;
        }

        if ( next_html_resource )
        {
            if ( ResourceContainsCurrentRegex( next_html_resource ) )
            {
                return next_html_resource;
            }
            // else continue
        }
        else
            return NULL;
    }
    return NULL;
}


HTMLResource* FindReplace::GetNextHTMLResource( HTMLResource *current_resource, Searchable::Direction direction )
{
    QList <Resource *> resources = GetHTMLFiles();

    int max_reading_order       = resources.count() - 1;
    int current_reading_order   = 0;
    int next_reading_order      = 0;

    // Find the current resource in the selected/all html entries
    int i = 0;
    foreach ( Resource *resource, resources )
    {
        if ( resource->Filename() == current_resource->Filename() )
        {
            current_reading_order = i;
            break;
        }
        i++;
    }

    // We wrap back (if needed)
    if ( direction == Searchable::Direction_Up )
    {
        next_reading_order = current_reading_order - 1 >= 0 ? current_reading_order - 1 : max_reading_order ;
    }
    else
    {
        next_reading_order = current_reading_order + 1 <= max_reading_order ? current_reading_order + 1 : 0;
    }

    if ( next_reading_order > max_reading_order || next_reading_order < 0 )
    {
        return NULL;
    }
    else
    {
        HTMLResource &html_resource = *qobject_cast< HTMLResource *>( resources[ next_reading_order ] );
        return &html_resource;
    }
}


Resource* FindReplace::GetCurrentResource()
{
    return &m_MainWindow.GetCurrentContentTab().GetLoadedResource();
}


QStringList FindReplace::GetPreviousFindStrings()
{
    QStringList find_strings;

    for ( int i = 0; i < qMin( ui.cbFind->count(), MAX_HISTORY_COUNT ); ++i )
    {
        if ( !find_strings.contains( ui.cbFind->itemText( i ) ) )
        {
            find_strings.append( ui.cbFind->itemText( i ) );
        }
    }

    return find_strings;
}


QStringList FindReplace::GetPreviousReplaceStrings()
{
    QStringList replace_strings;

    for ( int i = 0; i < qMin( ui.cbReplace->count(), MAX_HISTORY_COUNT ); ++i )
    {
        if ( !replace_strings.contains( ui.cbReplace->itemText( i ) ) )
        {
            replace_strings.append( ui.cbReplace->itemText( i ) );
        }
    }

    return replace_strings;
}


void FindReplace::UpdatePreviousFindStrings()
{
    QString new_find_string = ui.cbFind->lineEdit()->text();
    int used_at_index = ui.cbFind->findText( new_find_string );

    if ( used_at_index != -1 )
    {
        ui.cbFind->removeItem( used_at_index );
    }

    ui.cbFind->insertItem( 0, new_find_string );

    // Must not change the current string!
    ui.cbFind->setCurrentIndex( 0 );
}


void FindReplace::UpdatePreviousReplaceStrings()
{
    QString new_replace_string = ui.cbReplace->lineEdit()->text();
    int used_at_index = ui.cbReplace->findText( new_replace_string );

    if ( used_at_index != -1 )
    {
        ui.cbReplace->removeItem( used_at_index );
    }

    ui.cbReplace->insertItem( 0, new_replace_string );

    // Must not change the current string!
    ui.cbReplace->setCurrentIndex( 0 );
}


FindReplace::LookWhere FindReplace::GetLookWhere()
{
    int look = ui.cbLookWhere->itemData( ui.cbLookWhere->currentIndex() ).toInt();
    switch ( look )
    {
    case FindReplace::LookWhere_AllHTMLFiles:
        return static_cast<FindReplace::LookWhere>( look );
        break;
    case FindReplace::LookWhere_SelectedHTMLFiles:
        return static_cast<FindReplace::LookWhere>( look );
        break;
    default:
        return FindReplace::LookWhere_CurrentFile;
    }
}


FindReplace::SearchMode FindReplace::GetSearchMode()
{
    int mode = ui.cbSearchMode->itemData( ui.cbSearchMode->currentIndex() ).toInt();
    switch ( mode )
    {
    case FindReplace::SearchMode_Regex:
        return static_cast<FindReplace::SearchMode>( mode );
        break;
    case FindReplace::SearchMode_Case_Sensitive:
        return static_cast<FindReplace::SearchMode>( mode );
        break;
    case FindReplace::SearchMode_RegexDotall:
        return static_cast<FindReplace::SearchMode>( mode );
        break;
    case FindReplace::SearchMode_SpellCheck:
        return static_cast<FindReplace::SearchMode>( mode );
        break;
    default:
        return FindReplace::SearchMode_Normal;
    }
}


FindReplace::SearchDirection FindReplace::GetSearchDirection()
{
    int direction = ui.cbSearchDirection->itemData( ui.cbSearchDirection->currentIndex() ).toInt();
    switch ( direction )
    {
    case FindReplace::SearchDirection_Up:
        return static_cast<FindReplace::SearchDirection>( direction );
        break;
    default:
        return FindReplace::SearchDirection_Down;
    }
}


bool FindReplace::IsValidFindText()
{
    return  !ui.cbFind->lineEdit()->text().isEmpty() || GetSearchMode() == SearchMode_SpellCheck;
}


// Reads all the stored dialog settings like
// window position, geometry etc.
void FindReplace::ReadSettings()
{
    SettingsStore settings;
    settings.beginGroup( SETTINGS_GROUP );

    // Input fields
    QStringList find_strings = settings.value( "find_strings" ).toStringList();
    find_strings.removeDuplicates();
    ui.cbFind->addItems( find_strings );

    find_strings = settings.value( "replace_strings" ).toStringList();
    find_strings.removeDuplicates();
    ui.cbReplace->addItems( find_strings );

    ui.cbSearchMode->setCurrentIndex( 0 );
    int search_mode = settings.value( "search_mode", 0 ).toInt();
    for ( int i = 0; i < ui.cbSearchMode->count(); ++i )
    {
        if ( ui.cbSearchMode->itemData( i ) == search_mode )
        {
            ui.cbSearchMode->setCurrentIndex( i );
            break;
        }
    }

    ui.cbLookWhere->setCurrentIndex( 0 );
    int look_where = settings.value( "look_where", 0 ).toInt();
    for ( int i = 0; i < ui.cbLookWhere->count(); ++i )
    {
        if ( ui.cbLookWhere->itemData( i )  == look_where )
        {
            ui.cbLookWhere->setCurrentIndex( i );
            break;
        }
    }

    ui.cbSearchDirection->setCurrentIndex( 0 );
    int search_direction= settings.value( "search_direction", 0 ).toInt();
    for ( int i = 0; i < ui.cbSearchDirection->count(); ++i )
    {
        if ( ui.cbSearchDirection->itemData( i ) == search_direction )
        {
            ui.cbSearchDirection->setCurrentIndex( i );
            break;
        }
    }

    settings.endGroup();
}


// Writes all the stored dialog settings like
// window position, geometry etc.
void FindReplace::WriteSettings()
{
    SettingsStore settings;
    settings.beginGroup( SETTINGS_GROUP );

    settings.setValue( "find_strings", GetPreviousFindStrings() );
    settings.setValue( "replace_strings", GetPreviousReplaceStrings() );

    settings.setValue( "search_mode", GetSearchMode() );
    settings.setValue( "look_where", GetLookWhere() );
    settings.setValue( "search_direction", GetSearchDirection() );

    settings.endGroup();
}


Searchable* FindReplace::GetAvailableSearchable()
{
    Searchable *searchable = m_MainWindow.GetCurrentContentTab().GetSearchableContent();

    if ( !searchable )
    {
        ShowMessage( tr( "This tab cannot be searched" ) );
    }

    return searchable;
}


void FindReplace::ExtendUI()
{
    ui.cbFind->setCompleter( 0 );
    ui.cbReplace->setCompleter( 0 );

    ui.cbSearchMode->addItem( tr( "Normal" ), FindReplace::SearchMode_Normal );
    ui.cbSearchMode->addItem( tr( "Case Sensitive" ), FindReplace::SearchMode_Case_Sensitive );
    ui.cbSearchMode->addItem( tr( "Regex" ), FindReplace::SearchMode_Regex );
    ui.cbSearchMode->addItem( tr( "Regex Dotall" ), FindReplace::SearchMode_RegexDotall );
    ui.cbSearchMode->addItem( tr( "Spell Check" ), FindReplace::SearchMode_SpellCheck );
    ui.cbSearchMode->setToolTip( tr( "<p>Mode:</p>"
        "<dl>"
        "<dt><b>Normal</b><dd>Case in-sensitive search of exactly what you type</dd>"
        "<dt><b>Case Sensitive</b><dd>Case sensitive search of exactly what you type</dd>"
        "<dt><b>Regex</b><dd>Search for a pattern using Regular Expression syntax</dd>"
        "<dt><b>Regex Dotall</b><dd>Search using Regular Expression syntax but automatically allow <b>.*</b> to match new lines</dd>"
        "<dt><b>Spell Check</b><dd>Search only misspelled words using Regular Expression syntax, or clear the Find text box to find all misspelled words</dd>"
        "</dl>" ) );

    ui.cbLookWhere->addItem( tr( "Current File" ), FindReplace::LookWhere_CurrentFile );
    ui.cbLookWhere->addItem( tr( "All HTML Files" ), FindReplace::LookWhere_AllHTMLFiles );
    ui.cbLookWhere->addItem( tr( "Selected HTML Files" ), FindReplace::LookWhere_SelectedHTMLFiles );
    ui.cbLookWhere->setToolTip( tr( "<p>Look</p>"
        "<dl>"
        "<dt><b>Current File</b><dd>Restrict the find or replace to the opened file</dd>"
        "<dt><b>All HTML Files</b><dd>Find or replace in all HTML files - always done in Code View</dd>"
        "<dt><b>Selected HTML Files</b><dd>Restrict the find or replace to the HTML files selected in the Book Browser - always done in Code View</dd>"
        "</dl>" ) );

    ui.cbSearchDirection->addItem( tr( "Up" ), FindReplace::SearchDirection_Up );
    ui.cbSearchDirection->addItem( tr( "Down" ), FindReplace::SearchDirection_Down );
    ui.cbSearchDirection->setToolTip( tr( "<p>Direction:</p>"
        "<dl>"
        "<dt><b>Up</b><dd>Search for the previous match from your current position</dd>"
        "<dt><b>Down</b><dd>Search for the next match from your current position</dd>"
        "</dl>" ) );
}


void FindReplace::ConnectSignalsToSlots()
{
    connect( &m_timer, SIGNAL( timeout() ), this, SLOT( expireMessage() ) );
    connect( ui.findNext, SIGNAL( clicked() ), this, SLOT( Find() ) );
    connect( ui.cbFind->lineEdit(), SIGNAL( returnPressed() ), this, SLOT( Find() ) );
    connect( ui.count, SIGNAL( clicked() ), this, SLOT( Count() ) );
    connect( ui.replaceNext, SIGNAL( clicked() ), this, SLOT( Replace() ) );
    connect( ui.cbReplace->lineEdit(), SIGNAL( returnPressed() ), this, SLOT( Replace() ));
    connect( ui.replaceAll, SIGNAL( clicked() ), this, SLOT( ReplaceAll() ) );
    connect( ui.close, SIGNAL( clicked() ), this, SLOT( hide() ) );
}
