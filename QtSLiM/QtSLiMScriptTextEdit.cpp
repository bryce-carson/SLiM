//
//  QtSLiMScriptTextEdit.cpp
//  SLiM
//
//  Created by Ben Haller on 11/24/2019.
//  Copyright (c) 2019 Philipp Messer.  All rights reserved.
//	A product of the Messer Lab, http://messerlab.org/slim/
//

//	This file is part of SLiM.
//
//	SLiM is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
//
//	SLiM is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along with SLiM.  If not, see <http://www.gnu.org/licenses/>.


#include "QtSLiMScriptTextEdit.h"

#include <QApplication>
#include <QGuiApplication>
#include <QTextCursor>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QStyle>
#include <QAbstractTextDocumentLayout>
#include <QMessageBox>
#include <QSettings>
#include <QCheckBox>
#include <QMainWindow>
#include <QStatusBar>
#include <QDebug>

#include "QtSLiMPreferences.h"
#include "QtSLiMEidosPrettyprinter.h"
#include "QtSLiMSyntaxHighlighting.h"
#include "QtSLiMEidosConsole.h"
#include "QtSLiMHelpWindow.h"
#include "QtSLiMAppDelegate.h"
#include "QtSLiMWindow.h"
#include "QtSLiM_SLiMgui.h"

#include "eidos_script.h"
#include "eidos_token.h"
#include "slim_eidos_block.h"
#include "subpopulation.h"


//
//  QtSLiMTextEdit
//

QtSLiMTextEdit::QtSLiMTextEdit(const QString &text, QWidget *parent) : QTextEdit(text, parent)
{
    selfInit();
}

QtSLiMTextEdit::QtSLiMTextEdit(QWidget *parent) : QTextEdit(parent)
{
    selfInit();
}

void QtSLiMTextEdit::selfInit(void)
{
    // clear the custom error background color whenever the selection changes
    connect(this, &QTextEdit::selectionChanged, [this]() { setPalette(style()->standardPalette()); });
    connect(this, &QTextEdit::cursorPositionChanged, [this]() { setPalette(style()->standardPalette()); });
    
    // clear the status bar on a selection change; FIXME upgrade this to updateStatusFieldFromSelection() eventually...
    connect(this, &QTextEdit::selectionChanged, this, &QtSLiMTextEdit::updateStatusFieldFromSelection);
    connect(this, &QTextEdit::cursorPositionChanged, this, &QtSLiMTextEdit::updateStatusFieldFromSelection);
    
    // Wire up to change the font when the display font pref changes
    QtSLiMPreferencesNotifier &prefsNotifier = QtSLiMPreferencesNotifier::instance();
    
    connect(&prefsNotifier, &QtSLiMPreferencesNotifier::displayFontPrefChanged, this, &QtSLiMTextEdit::displayFontPrefChanged);
    connect(&prefsNotifier, &QtSLiMPreferencesNotifier::scriptSyntaxHighlightPrefChanged, this, &QtSLiMTextEdit::scriptSyntaxHighlightPrefChanged);
    connect(&prefsNotifier, &QtSLiMPreferencesNotifier::outputSyntaxHighlightPrefChanged, this, &QtSLiMTextEdit::outputSyntaxHighlightPrefChanged);
    
    // Get notified of modifier key changes, so we can change our cursor
    connect(qtSLiMAppDelegate, &QtSLiMAppDelegate::modifiersChanged, this, &QtSLiMTextEdit::modifiersChanged);
    
    // set up the script and output textedits
    QtSLiMPreferencesNotifier &prefs = QtSLiMPreferencesNotifier::instance();
    int tabWidth = 0;
    QFont scriptFont = prefs.displayFontPref(&tabWidth);
    
    setFont(scriptFont);
    setTabStopWidth(tabWidth);    // should use setTabStopDistance(), which requires Qt 5.10; see https://stackoverflow.com/a/54605709/2752221
}

QtSLiMTextEdit::~QtSLiMTextEdit()
{
}

void QtSLiMTextEdit::setScriptType(ScriptType type)
{
    // Configure our script type; this should be called once, early
    scriptType = type;
}

void QtSLiMTextEdit::setSyntaxHighlightType(ScriptHighlightingType type)
{
    // Configure our syntax highlighting; this should be called once, early
    syntaxHighlightingType = type;
    
    scriptSyntaxHighlightPrefChanged();     // create a highlighter if needed
    outputSyntaxHighlightPrefChanged();     // create a highlighter if needed
}

void QtSLiMTextEdit::setOptionClickEnabled(bool enabled)
{
    optionClickEnabled = enabled;
    optionClickIntercepted = false;
}

void QtSLiMTextEdit::displayFontPrefChanged()
{
    QtSLiMPreferencesNotifier &prefs = QtSLiMPreferencesNotifier::instance();
    int tabWidth = 0;
    QFont displayFont = prefs.displayFontPref(&tabWidth);
    
    setFont(displayFont);
    setTabStopWidth(tabWidth);
}

void QtSLiMTextEdit::scriptSyntaxHighlightPrefChanged()
{
    if (syntaxHighlightingType == QtSLiMTextEdit::ScriptHighlighting)
    {
        QtSLiMPreferencesNotifier &prefs = QtSLiMPreferencesNotifier::instance();
        bool highlightPref = prefs.scriptSyntaxHighlightPref();
        
        if (highlightPref && !scriptHighlighter)
        {
            scriptHighlighter = new QtSLiMScriptHighlighter(document());
        }
        else if (!highlightPref && scriptHighlighter)
        {
            scriptHighlighter->setDocument(nullptr);
            scriptHighlighter->setParent(nullptr);
            delete scriptHighlighter;
            scriptHighlighter = nullptr;
        }
    }
}

void QtSLiMTextEdit::outputSyntaxHighlightPrefChanged()
{
    if (syntaxHighlightingType == QtSLiMTextEdit::OutputHighlighting)
    {
        QtSLiMPreferencesNotifier &prefs = QtSLiMPreferencesNotifier::instance();
        bool highlightPref = prefs.outputSyntaxHighlightPref();
        
        if (highlightPref && !outputHighlighter)
        {
            outputHighlighter = new QtSLiMOutputHighlighter(document());
        }
        else if (!highlightPref && outputHighlighter)
        {
            outputHighlighter->setDocument(nullptr);
            outputHighlighter->setParent(nullptr);
            delete outputHighlighter;
            outputHighlighter = nullptr;
        }
    }
}

void QtSLiMTextEdit::highlightError(int startPosition, int endPosition)
{
    QTextCursor cursor(document());
    
    cursor.setPosition(startPosition);
    cursor.setPosition(endPosition, QTextCursor::KeepAnchor);
    setTextCursor(cursor);
    
    QPalette p = palette();
    p.setColor(QPalette::Highlight, QColor(QColor(Qt::red).lighter(120)));
    p.setColor(QPalette::HighlightedText, QColor(Qt::black));
    setPalette(p);
    
    // note that this custom selection color is cleared by a connection to QTextEdit::selectionChanged()
}

void QtSLiMTextEdit::selectErrorRange(void)
{
	// If there is error-tracking information set, and the error is not attributed to a runtime script
	// such as a lambda or a callback, then we can highlight the error range
	if (!gEidosExecutingRuntimeScript && (gEidosCharacterStartOfErrorUTF16 >= 0) && (gEidosCharacterEndOfErrorUTF16 >= gEidosCharacterStartOfErrorUTF16))
        highlightError(gEidosCharacterStartOfErrorUTF16, gEidosCharacterEndOfErrorUTF16 + 1);
	
	// In any case, since we are the ultimate consumer of the error information, we should clear out
	// the error state to avoid misattribution of future errors
	gEidosCharacterStartOfError = -1;
	gEidosCharacterEndOfError = -1;
	gEidosCharacterStartOfErrorUTF16 = -1;
	gEidosCharacterEndOfErrorUTF16 = -1;
	gEidosCurrentScript = nullptr;
	gEidosExecutingRuntimeScript = false;
}

QStatusBar *QtSLiMTextEdit::statusBarForWindow(void)
{
    // This is a bit of a hack because the console window is not a MainWindow subclass, and makes its own status bar
    QWidget *ourWindow = window();
    QMainWindow *mainWindow = dynamic_cast<QMainWindow *>(ourWindow);
    QtSLiMEidosConsole *consoleWindow = dynamic_cast<QtSLiMEidosConsole *>(ourWindow);
    QStatusBar *statusBar = nullptr;
    
    if (mainWindow)
        statusBar = mainWindow->statusBar();
    else if (consoleWindow)
        statusBar = consoleWindow->statusBar();
    
    return statusBar;
}

QtSLiMWindow *QtSLiMTextEdit::slimControllerForWindow(void)
{
    QtSLiMWindow *windowSLiMController = dynamic_cast<QtSLiMWindow *>(window());
    
    if (windowSLiMController)
        return windowSLiMController;
    
    QtSLiMEidosConsole *windowEidosConsole = dynamic_cast<QtSLiMEidosConsole *>(window());
    
    if (windowEidosConsole)
        return windowEidosConsole->parentSLiMWindow;
    
    return nullptr;
}

bool QtSLiMTextEdit::checkScriptSuppressSuccessResponse(bool suppressSuccessResponse)
{
	// Note this does *not* check out scriptString, which represents the state of the script when the SLiMSim object was created
	// Instead, it checks the current script in the script TextView – which is not used for anything until the recycle button is clicked.
	QString currentScriptString = toPlainText();
    QByteArray utf8bytes = currentScriptString.toUtf8();
	const char *cstr = utf8bytes.constData();
	std::string errorDiagnostic;
	
	if (!cstr)
	{
		errorDiagnostic = "The script string could not be read, possibly due to an encoding problem.";
	}
	else
	{
        if (scriptType == EidosScriptType)
        {
            EidosScript script(cstr);
            
            try {
                script.Tokenize();
                script.ParseInterpreterBlockToAST(true);
            }
            catch (...)
            {
                errorDiagnostic = Eidos_GetTrimmedRaiseMessage();
            }
        }
        else if (scriptType == SLiMScriptType)
        {
            SLiMEidosScript script(cstr);
            
            try {
                script.Tokenize();
                script.ParseSLiMFileToAST();
            }
            catch (...)
            {
                errorDiagnostic = Eidos_GetTrimmedRaiseMessage();
            }
        }
        else
        {
            qDebug() << "checkScriptSuppressSuccessResponse() called with no script type set";
        }
	}
	
	bool checkDidSucceed = !(errorDiagnostic.length());
	
	if (!checkDidSucceed || !suppressSuccessResponse)
	{
		if (!checkDidSucceed)
		{
			// On failure, we show an alert describing the error, and highlight the relevant script line
            qApp->beep();
            selectErrorRange();
            
            QString q_errorDiagnostic = QString::fromStdString(errorDiagnostic);
            QMessageBox messageBox(this);
            messageBox.setText("Script error");
            messageBox.setInformativeText(q_errorDiagnostic);
            messageBox.setIcon(QMessageBox::Warning);
            messageBox.setWindowModality(Qt::WindowModal);
            messageBox.setFixedWidth(700);      // seems to be ignored
            messageBox.exec();
            
			// Show the error in the status bar also
            QStatusBar *statusBar = statusBarForWindow();
            
            if (statusBar)
            {
                statusBar->setStyleSheet("color: #cc0000; font-size: 11px;");
                statusBar->showMessage(q_errorDiagnostic.trimmed());
            }
		}
		else
		{
            QSettings settings;
            
            if (!settings.value("QtSLiMSuppressScriptCheckSuccessPanel", false).toBool())
			{
                // In SLiMgui we play a "success" sound too, but doing anything besides beeping is apparently difficult with Qt...
                
                QMessageBox messageBox(this);
                messageBox.setText("No script errors");
                messageBox.setInformativeText("No errors found.");
                messageBox.setIcon(QMessageBox::Information);
                messageBox.setWindowModality(Qt::WindowModal);
                messageBox.setCheckBox(new QCheckBox("Do not show this message again", nullptr));
                messageBox.exec();
                
                if (messageBox.checkBox()->isChecked())
                    settings.setValue("QtSLiMSuppressScriptCheckSuccessPanel", true);
            }
		}
	}
	
	return checkDidSucceed;
}

void QtSLiMTextEdit::checkScript(void)
{
    checkScriptSuppressSuccessResponse(false);
}

void QtSLiMTextEdit::prettyprint(void)
{
    if (isEnabled())
	{
		if (checkScriptSuppressSuccessResponse(true))
		{
			// We know the script is syntactically correct, so we can tokenize and parse it without worries
            QString currentScriptString = toPlainText();
            QByteArray utf8bytes = currentScriptString.toUtf8();
            const char *cstr = utf8bytes.constData();
			EidosScript script(cstr);
            
			script.Tokenize(false, true);	// get whitespace and comment tokens
			
			// Then generate a new script string that is prettyprinted
			const std::vector<EidosToken> &tokens = script.Tokens();
			std::string pretty;
			
            if (Eidos_prettyprintTokensFromScript(tokens, script, pretty))
                setPlainText(QString::fromStdString(pretty));
			else
                qApp->beep();
		}
	}
	else
	{
        qApp->beep();
	}
}

void QtSLiMTextEdit::scriptHelpOptionClick(QString searchString)
{
    QtSLiMHelpWindow &helpWindow = QtSLiMHelpWindow::instance();
    
    // A few Eidos substitutions to improve the search
    if (searchString == ":")                    searchString = "operator :";
    else if (searchString == "(")               searchString = "operator ()";
    else if (searchString == ")")               searchString = "operator ()";
    else if (searchString == ",")               searchString = "calls: operator ()";
    else if (searchString == "[")               searchString = "operator []";
    else if (searchString == "]")               searchString = "operator []";
    else if (searchString == "{")               searchString = "compound statements";
    else if (searchString == "}")               searchString = "compound statements";
    else if (searchString == ".")               searchString = "operator .";
    else if (searchString == "=")               searchString = "operator =";
    else if (searchString == "+")               searchString = "Arithmetic operators";
    else if (searchString == "-")               searchString = "Arithmetic operators";
    else if (searchString == "*")               searchString = "Arithmetic operators";
    else if (searchString == "/")               searchString = "Arithmetic operators";
    else if (searchString == "%")               searchString = "Arithmetic operators";
    else if (searchString == "^")               searchString = "Arithmetic operators";
    else if (searchString == "|")               searchString = "Logical operators";
    else if (searchString == "&")               searchString = "Logical operators";
    else if (searchString == "!")               searchString = "Logical operators";
    else if (searchString == "==")              searchString = "Comparative operators";
    else if (searchString == "!=")              searchString = "Comparative operators";
    else if (searchString == "<=")              searchString = "Comparative operators";
    else if (searchString == ">=")              searchString = "Comparative operators";
    else if (searchString == "<")               searchString = "Comparative operators";
    else if (searchString == ">")               searchString = "Comparative operators";
    else if (searchString == "'")               searchString = "type string";
    else if (searchString == "\"")              searchString = "type string";
    else if (searchString == ";")               searchString = "null statements";
    else if (searchString == "//")              searchString = "comments";
    else if (searchString == "if")              searchString = "if and if–else statements";
    else if (searchString == "else")            searchString = "if and if–else statements";
    else if (searchString == "for")             searchString = "for statements";
    else if (searchString == "in")              searchString = "for statements";
    else if (searchString == "function")        searchString = "user-defined functions";
    // and SLiM substitutions; "initialize" is deliberately omitted here so that the initialize...() methods also come up
    else if (searchString == "early")			searchString = "Eidos events";
	else if (searchString == "late")			searchString = "Eidos events";
	else if (searchString == "fitness")         searchString = "fitness() callbacks";
	else if (searchString == "interaction")     searchString = "interaction() callbacks";
	else if (searchString == "mateChoice")      searchString = "mateChoice() callbacks";
	else if (searchString == "modifyChild")     searchString = "modifyChild() callbacks";
	else if (searchString == "recombination")	searchString = "recombination() callbacks";
	else if (searchString == "mutation")		searchString = "mutation() callbacks";
	else if (searchString == "reproduction")	searchString = "reproduction() callbacks";
    
    // now send the search string on to the help window
    helpWindow.enterSearchForString(searchString, true);
}

void QtSLiMTextEdit::mousePressEvent(QMouseEvent *event)
{
    bool optionPressed = (optionClickEnabled && QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier));
    
    if (optionPressed)
    {
        // option-click gets intercepted to bring up help
        optionClickIntercepted = true;
        
        // get the position of the character clicked on; note that this is different from
        // QTextEdit::cursorForPosition(), which returns the closest cursor position
        // *between* characters, not which character was actually clicked on; see
        // https://www.qtcentre.org/threads/45645-QTextEdit-cursorForPosition()-and-character-at-mouse-pointer
        int characterPositionClicked = document()->documentLayout()->hitTest(event->localPos(), Qt::ExactHit);
        
        if (characterPositionClicked == -1)     // occurs if you click between lines of text
            return;
        
        QTextCursor charCursor(document());
        charCursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, characterPositionClicked);
        charCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
        
        QString characterString = charCursor.selectedText();
        
        if (characterString.length() != 1)      // not sure if this ever happens, being safe
            return;
        
        QChar character = characterString.at(0);
        
        if (character.isSpace())                // no help on whitespace
            return;
        
        //qDebug() << characterPositionClicked << ": " << charCursor.anchor() << "," << charCursor.position() << "," << charCursor.selectedText();
        
        // if the character is a letter or number, we want to select the word it
        // is contained by and use that as the symbol for lookup; otherwise, 
        // it is symbolic, and we want to try to match the right symbol in the code
        QTextCursor symbolCursor(charCursor);
        
        if (character.isLetterOrNumber())
        {
            symbolCursor.select(QTextCursor::WordUnderCursor);
        }
        else if ((character == '/') || (character == '=') || (character == '<') || (character == '>') || (character == '!'))
        {
            // the character clicked might be part of a multicharacter symbol: // == <= >= !=
            // we will look at two-character groups anchored in the clicked character to test this
            QTextCursor leftPairCursor(document()), rightPairCursor(document());
            leftPairCursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, characterPositionClicked - 1);
            leftPairCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
            rightPairCursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, characterPositionClicked);
            rightPairCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
            
            QString leftPairString = leftPairCursor.selectedText(), rightPairString = rightPairCursor.selectedText();
            
            if ((leftPairString == "//") || (leftPairString == "==") || (leftPairString == "<=") || (leftPairString == ">=") || (leftPairString == "!="))
                symbolCursor = leftPairCursor;
            else if ((rightPairString == "//") || (rightPairString == "==") || (rightPairString == "<=") || (rightPairString == ">=") || (rightPairString == "!="))
                symbolCursor = rightPairCursor;
            // else we drop through and search for the one-character symbol
        }
        else
        {
            // the character clicked is a one-character symbol; we just drop through
        }
        
        // select the symbol and trigger a lookup
        QString symbol = symbolCursor.selectedText();
        
        if (symbol.length())
        {
            setTextCursor(symbolCursor);
            scriptHelpOptionClick(symbol);
        }
    }
    else
    {
        // all other cases go to super
        optionClickIntercepted = false;
        
        QTextEdit::mousePressEvent(event);
    }
}

void QtSLiMTextEdit::mouseMoveEvent(QMouseEvent *event)
{
    // forward to super, as long as we did not intercept this mouse event
    if (!optionClickIntercepted)
        QTextEdit::mouseMoveEvent(event);
}

void QtSLiMTextEdit::mouseReleaseEvent(QMouseEvent *event)
{
    // forward to super, as long as we did not intercept this mouse event
    if (!optionClickIntercepted)
        QTextEdit::mouseReleaseEvent(event);
    
    optionClickIntercepted = false;
}

void QtSLiMTextEdit::fixMouseCursor(void)
{
    if (optionClickEnabled)
    {
        // we want a pointing hand cursor when option is pressed; if the cursor is wrong, fix it
        // note the cursor for QTextEdit is apparently controlled by its viewport
        bool optionPressed = QGuiApplication::queryKeyboardModifiers().testFlag(Qt::AltModifier);
        QWidget *vp = viewport();
        
        if (optionPressed && (vp->cursor().shape() != Qt::PointingHandCursor))
            vp->setCursor(Qt::PointingHandCursor);
        else if (!optionPressed && (vp->cursor().shape() != Qt::IBeamCursor))
            vp->setCursor(Qt::IBeamCursor);
    }
}

void QtSLiMTextEdit::enterEvent(QEvent *event)
{
    // forward to super
    QTextEdit::enterEvent(event);
    
    // modifiersChanged() generally keeps our cursor correct, but we do it on enterEvent
    // as well just as a fallback; for example, if the mouse is inside us on launch and
    // the modifier is already down, enterEvent() will fix out initial cursor
    fixMouseCursor();
}

void QtSLiMTextEdit::modifiersChanged(Qt::KeyboardModifiers __attribute__((unused)) newModifiers)
{
    // keyPressEvent() and keyReleaseEvent() are sent to us only when we have the focus, but
    // we want to change our cursor even when we don't have focus, so we use an event filter
    fixMouseCursor();
}

const EidosCallSignature *QtSLiMTextEdit::signatureForFunctionName(QString callName, EidosFunctionMap *functionMapPtr)
{
	std::string call_name = callName.toStdString();
	
	// Look for a matching function signature for the call name.
	for (const auto& function_iter : *functionMapPtr)
	{
		const EidosFunctionSignature *sig = function_iter.second.get();
		const std::string &sig_call_name = sig->call_name_;
		
		if (sig_call_name.compare(call_name) == 0)
			return sig;
	}
	
	return nullptr;
}

const std::vector<const EidosMethodSignature*> *QtSLiMTextEdit::slimguiAllMethodSignatures(void)
{
	// This adds the methods belonging to the SLiMgui class to those returned by SLiMSim (which does not know about SLiMgui)
	static std::vector<const EidosMethodSignature*> *methodSignatures = nullptr;
	
	if (!methodSignatures)
	{
		auto slimMethods =					SLiMSim::AllMethodSignatures();
		auto methodsSLiMgui =				gSLiM_SLiMgui_Class->Methods();
		
		methodSignatures = new std::vector<const EidosMethodSignature*>(*slimMethods);
		
		methodSignatures->insert(methodSignatures->end(), methodsSLiMgui->begin(), methodsSLiMgui->end());
		
		// *** From here downward this is taken verbatim from SLiMSim::AllMethodSignatures()
		// FIXME should be split into a separate method
		
		// sort by pointer; we want pointer-identical signatures to end up adjacent
		std::sort(methodSignatures->begin(), methodSignatures->end());
		
		// then unique by pointer value to get a list of unique signatures (which may not be unique by name)
		auto unique_end_iter = std::unique(methodSignatures->begin(), methodSignatures->end());
		methodSignatures->resize(static_cast<size_t>(std::distance(methodSignatures->begin(), unique_end_iter)));
		
		// print out any signatures that are identical by name
		std::sort(methodSignatures->begin(), methodSignatures->end(), CompareEidosCallSignatures);
		
		const EidosMethodSignature *previous_sig = nullptr;
		
		for (const EidosMethodSignature *sig : *methodSignatures)
		{
			if (previous_sig && (sig->call_name_.compare(previous_sig->call_name_) == 0))
			{
				// We have a name collision.  That is OK as long as the method signatures are identical.
				if ((typeid(*sig) != typeid(*previous_sig)) ||
					(sig->is_class_method != previous_sig->is_class_method) ||
					(sig->call_name_ != previous_sig->call_name_) ||
					(sig->return_mask_ != previous_sig->return_mask_) ||
					(sig->return_class_ != previous_sig->return_class_) ||
					(sig->arg_masks_ != previous_sig->arg_masks_) ||
					(sig->arg_names_ != previous_sig->arg_names_) ||
					(sig->arg_classes_ != previous_sig->arg_classes_) ||
					(sig->has_optional_args_ != previous_sig->has_optional_args_) ||
					(sig->has_ellipsis_ != previous_sig->has_ellipsis_))
				std::cout << "Duplicate method name with a different signature: " << sig->call_name_ << std::endl;
			}
			
			previous_sig = sig;
		}
		
		// log a full list
		//std::cout << "----------------" << std::endl;
		//for (const EidosMethodSignature *sig : *methodSignatures)
		//	std::cout << sig->call_name_ << " (" << sig << ")" << std::endl;
	}
	
	return methodSignatures;
}

const EidosCallSignature *QtSLiMTextEdit::signatureForMethodName(QString callName)
{
	std::string call_name = callName.toStdString();
	
	// Look for a method in the global method registry last; for this to work, the Context must register all methods with Eidos.
	// This case is much simpler than the function case, because the user can't declare their own methods.
	const std::vector<const EidosMethodSignature *> *methodSignatures = nullptr;
	
    methodSignatures = slimguiAllMethodSignatures();
    
	if (!methodSignatures)
		methodSignatures = gEidos_UndefinedClassObject->Methods();
	
	for (const EidosMethodSignature *sig : *methodSignatures)
	{
		const std::string &sig_call_name = sig->call_name_;
		
		if (sig_call_name.compare(call_name) == 0)
			return sig;
	}
	
	return nullptr;
}

EidosFunctionMap *QtSLiMTextEdit::functionMapForTokenizedScript(EidosScript &script)
{
    // This lower-level function takes a tokenized script object and works from there, allowing reuse of work
    // in the case of attributedSignatureForScriptString:...
    QtSLiMWindow *windowSLiMController = slimControllerForWindow();
    SLiMSim *sim = (windowSLiMController ? windowSLiMController->sim : nullptr);
    bool invalidSimulation = (windowSLiMController ? windowSLiMController->invalidSimulation() : true);
    
    // start with all the functions that are available in the current simulation context
    EidosFunctionMap *functionMapPtr = nullptr;
    
    if (sim && !invalidSimulation)
        functionMapPtr = new EidosFunctionMap(sim->FunctionMap());
    else
        functionMapPtr = new EidosFunctionMap(*EidosInterpreter::BuiltInFunctionMap());
    
    // functionMapForEidosTextView: returns the function map for the current interpreter state, and the type-interpreter
    // stuff we do below gives the delegate no chance to intervene (note that SLiMTypeInterpreter does not get in here,
    // unlike in the code completion machinery!).  But sometimes we want SLiM's zero-gen functions to be added to the map
    // in all cases; it would be even better to be smart the way code completion is, but that's more work than it's worth.
    if (!basedOnLiveSimulation)
    {
        // add SLiM functions that are context-dependent
        SLiMSim::AddZeroGenerationFunctionsToMap(*functionMapPtr);
        SLiMSim::AddSLiMFunctionsToMap(*functionMapPtr);
    }
    
    // OK, now we have a starting point.  We now want to use the type-interpreter to add any functions that are declared
    // in the full script, so that such declarations are known to us even before they have actually been executed.
    EidosTypeTable typeTable;
    EidosCallTypeTable callTypeTable;
    EidosSymbolTable *symbols = gEidosConstantsSymbolTable;
    
    if (sim && !invalidSimulation)
        symbols = sim->SymbolsFromBaseSymbols(symbols);
    
    if (symbols)
        symbols->AddSymbolsToTypeTable(&typeTable);
    
    script.ParseInterpreterBlockToAST(true, true);	// make bad nodes as needed (i.e. never raise, and produce a correct tree)
    
    EidosTypeInterpreter typeInterpreter(script, typeTable, *functionMapPtr, callTypeTable);
    
    typeInterpreter.TypeEvaluateInterpreterBlock();	// result not used
    
    return functionMapPtr;
}

void QtSLiMTextEdit::scriptStringAndSelection(QString &scriptString, int &pos, int &len)
{
    // by default, the entire contents of the textedit are considered "script"
    scriptString = toPlainText();
    
    QTextCursor selection(textCursor());
    pos = selection.selectionStart();
    len = selection.selectionEnd() - pos;
}

QString QtSLiMTextEdit::signatureStringForScriptSelection(QString &callName)
{
    // Note we return a copy of the signature, owned by the caller
    QString scriptString;
    int selectionStart, selectionEnd;
    
    scriptStringAndSelection(scriptString, selectionStart, selectionEnd);
    
    QString signatureString;
    
    if (scriptString.length())
	{
		std::string script_string = scriptString.toStdString();
		EidosScript script(script_string);
		
		// Tokenize
		script.Tokenize(true, false);	// make bad tokens as needed, don't keep nonsignificant tokens
		
		const std::vector<EidosToken> &tokens = script.Tokens();
		size_t tokenCount = tokens.size();
		
		// Search forward to find the token position of the start of the selection
		size_t tokenIndex;
		
		for (tokenIndex = 0; tokenIndex < tokenCount; ++tokenIndex)
			if (tokens[tokenIndex].token_UTF16_start_ >= selectionStart)
				break;
		
		// tokenIndex now has the index of the first token *after* the selection start; it can be equal to tokenCount
		// Now we want to scan backward from there, balancing parentheses and looking for the pattern "identifier("
		int backscanIndex = static_cast<int>(tokenIndex) - 1;
		int parenCount = 0, lowestParenCountSeen = 0;
		
		while (backscanIndex > 0)	// last examined position is 1, since we can't look for an identifier at 0 - 1 == -1
		{
			const EidosToken &token = tokens[static_cast<size_t>(backscanIndex)];
			EidosTokenType tokenType = token.token_type_;
			
			if (tokenType == EidosTokenType::kTokenLParen)
			{
				--parenCount;
				
				if (parenCount < lowestParenCountSeen)
				{
					const EidosToken &previousToken = tokens[static_cast<size_t>(backscanIndex) - 1];
					EidosTokenType previousTokenType = previousToken.token_type_;
					
					if (previousTokenType == EidosTokenType::kTokenIdentifier)
					{
						// OK, we found the pattern "identifier("; extract the name of the function/method
						// We also figure out here whether it is a method call (tokens like ".identifier(") or not
                        callName = QString::fromStdString(previousToken.token_string_);
                        
						if ((backscanIndex > 1) && (tokens[static_cast<size_t>(backscanIndex) - 2].token_type_ == EidosTokenType::kTokenDot))
						{
							// This is a method call, so look up its signature that way
							const EidosCallSignature *callSignature = signatureForMethodName(callName);
                            
                            if (callSignature)
                                signatureString = QString::fromStdString(callSignature->SignatureString());
                        }
						else
						{
							// If this is a function declaration like "function(...)identifier(" then show no signature; it's not a function call
							// Determining this requires a fairly complex backscan, because we also have things like "if (...) identifier(" which
							// are function calls.  This is the price we pay for working at the token level rather than the AST level for this;
							// so it goes.  Note that this backscan is separate from the one done outside this block.  BCH 1 March 2018.
							if ((backscanIndex > 1) && (tokens[static_cast<size_t>(backscanIndex) - 2].token_type_ == EidosTokenType::kTokenRParen))
							{
								// Start a new backscan starting at the right paren preceding the identifier; we need to scan back to the balancing
								// left paren, and then see if the next thing before that is "function" or not.
								int funcCheckIndex = backscanIndex - 2;
								int funcCheckParens = 0;
								
								while (funcCheckIndex >= 0)
								{
									const EidosToken &backscanToken = tokens[static_cast<size_t>(funcCheckIndex)];
									EidosTokenType backscanTokenType = backscanToken.token_type_;
									
									if (backscanTokenType == EidosTokenType::kTokenRParen)
										funcCheckParens++;
									else if (backscanTokenType == EidosTokenType::kTokenLParen)
										funcCheckParens--;
									
									--funcCheckIndex;
									
									if (funcCheckParens == 0)
										break;
								}
								
								if ((funcCheckParens == 0) && (funcCheckIndex >= 0) && (tokens[static_cast<size_t>(funcCheckIndex)].token_type_ == EidosTokenType::kTokenFunction))
									break;
							}
							
							// This is a function call, so look up its signature that way, using our best-guess function map
							EidosFunctionMap *functionMapPtr = functionMapForTokenizedScript(script);
                            const EidosCallSignature *callSignature = signatureForFunctionName(callName, functionMapPtr);
                            
                            if (callSignature)
                                signatureString = QString::fromStdString(callSignature->SignatureString());
                            
							delete functionMapPtr;  // note callSignature becomes invalid here; we cannot return it!
						}
                        
                        return signatureString;
					}
					
					lowestParenCountSeen = parenCount;
				}
			}
			else if (tokenType == EidosTokenType::kTokenRParen)
			{
				++parenCount;
			}
			
			--backscanIndex;
		}
	}
	
	return signatureString;
}

void QtSLiMTextEdit::updateStatusFieldFromSelection(void)
{
    if (scriptType != NoScriptType)
    {
        QString callName;
        QString displayString = signatureStringForScriptSelection(callName);
        
        if (!displayString.length() && (scriptType == SLiMScriptType))
        {
            // Handle SLiM callback signatures
            const EidosCallSignature *signature = nullptr;
            
            if (callName == "initialize")
            {
                static EidosCallSignature *callbackSig = nullptr;
                if (!callbackSig) callbackSig = (new EidosFunctionSignature("initialize", nullptr, kEidosValueMaskVOID));
                signature = callbackSig;
            }
            else if (callName == "early")
            {
                static EidosCallSignature *callbackSig = nullptr;
                if (!callbackSig) callbackSig = (new EidosFunctionSignature("early", nullptr, kEidosValueMaskVOID));
                signature = callbackSig;
            }
            else if (callName == "late")
            {
                static EidosCallSignature *callbackSig = nullptr;
                if (!callbackSig) callbackSig = (new EidosFunctionSignature("late", nullptr, kEidosValueMaskVOID));
                signature = callbackSig;
            }
            else if (callName == "fitness")
            {
                static EidosCallSignature *callbackSig = nullptr;
                if (!callbackSig) callbackSig = (new EidosFunctionSignature("fitness", nullptr, kEidosValueMaskFloat | kEidosValueMaskSingleton))->AddObject_SN("mutationType", gSLiM_MutationType_Class)->AddObject_OS("subpop", gSLiM_Subpopulation_Class, gStaticEidosValueNULLInvisible);
                signature = callbackSig;
            }
            else if (callName == "interaction")
            {
                static EidosCallSignature *callbackSig = nullptr;
                if (!callbackSig) callbackSig = (new EidosFunctionSignature("interaction", nullptr, kEidosValueMaskFloat | kEidosValueMaskSingleton))->AddObject_S("interactionType", gSLiM_InteractionType_Class)->AddObject_OS("subpop", gSLiM_Subpopulation_Class, gStaticEidosValueNULLInvisible);
                signature = callbackSig;
            }
            else if (callName == "mateChoice")
            {
                static EidosCallSignature *callbackSig = nullptr;
                if (!callbackSig) callbackSig = (new EidosFunctionSignature("mateChoice", nullptr, kEidosValueMaskNULL | kEidosValueMaskFloat | kEidosValueMaskObject, gSLiM_Individual_Class))->AddObject_OS("subpop", gSLiM_Subpopulation_Class, gStaticEidosValueNULLInvisible);
                signature = callbackSig;
            }
            else if (callName == "modifyChild")
            {
                static EidosCallSignature *callbackSig = nullptr;
                if (!callbackSig) callbackSig = (new EidosFunctionSignature("modifyChild", nullptr, kEidosValueMaskLogical | kEidosValueMaskSingleton))->AddObject_OS("subpop", gSLiM_Subpopulation_Class, gStaticEidosValueNULLInvisible);
                signature = callbackSig;
            }
            else if (callName == "recombination")
            {
                static EidosCallSignature *callbackSig = nullptr;
                if (!callbackSig) callbackSig = (new EidosFunctionSignature("recombination", nullptr, kEidosValueMaskLogical | kEidosValueMaskSingleton))->AddObject_OS("subpop", gSLiM_Subpopulation_Class, gStaticEidosValueNULLInvisible);
                signature = callbackSig;
            }
            else if (callName == "mutation")
            {
                static EidosCallSignature *callbackSig = nullptr;
                if (!callbackSig) callbackSig = (new EidosFunctionSignature("mutation", nullptr, kEidosValueMaskLogical | kEidosValueMaskSingleton))->AddObject_OSN("mutationType", gSLiM_MutationType_Class, gStaticEidosValueNULLInvisible)->AddObject_OSN("subpop", gSLiM_Subpopulation_Class, gStaticEidosValueNULLInvisible);
                signature = callbackSig;
            }
            else if (callName == "reproduction")
            {
                static EidosCallSignature *callbackSig = nullptr;
                if (!callbackSig) callbackSig = (new EidosFunctionSignature("reproduction", nullptr, kEidosValueMaskVOID))->AddObject_OSN("subpop", gSLiM_Subpopulation_Class, gStaticEidosValueNULLInvisible)->AddString_OSN("sex", gStaticEidosValueNULLInvisible);
                signature = callbackSig;
            }
            
            if (signature)
                displayString = QString::fromStdString(signature->SignatureString());
        }
        
        if (!displayString.length() && callName.length())
            displayString = callName + "() – unrecognized call";
        
        QStatusBar *statusBar = statusBarForWindow();
        
        if (displayString.length())
        {
            // FIXME no syntax coloring for this at the moment, because QStatusBar doesn't support rich text; subclass?
            // If we do get around to wanting to colorize, signatureStringForScriptSelection() should change to return
            // an EidosCallSignature*, but those will have to go under shared_ptr to make that work since user-defined
            // functions are not given a permanent signature in the present design.
            statusBar->setStyleSheet("font-size: 11px;");
            statusBar->showMessage(displayString);
        }
        else
        {
            statusBar->clearMessage();
        }
    }
}


//
//  QtSLiMScriptTextEdit
//

QtSLiMScriptTextEdit::~QtSLiMScriptTextEdit()
{
}

QStringList QtSLiMScriptTextEdit::linesForRoundedSelection(QTextCursor &cursor, bool &movedBack)
{
    // find the start and end of the blocks we're operating on
    int anchor = cursor.anchor(), pos = cursor.position();
    if (anchor > pos)
        std::swap(anchor, pos);
    movedBack = false;
    
    QTextCursor startBlockCursor(cursor);
    startBlockCursor.setPosition(anchor, QTextCursor::MoveAnchor);
    startBlockCursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::MoveAnchor);
    QTextCursor endBlockCursor(cursor);
    endBlockCursor.setPosition(pos, QTextCursor::MoveAnchor);
    if (endBlockCursor.atBlockStart() && (pos > anchor))
    {
        // the selection includes the newline at the end of the last line; we need to move backward to avoid swallowing the following line
        endBlockCursor.movePosition(QTextCursor::PreviousBlock, QTextCursor::MoveAnchor);
        movedBack = true;
    }
    endBlockCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::MoveAnchor);
    
    // select the whole lines we're operating on
    cursor.beginEditBlock();
    cursor.setPosition(startBlockCursor.position(), QTextCursor::MoveAnchor);
    cursor.setPosition(endBlockCursor.position(), QTextCursor::KeepAnchor);
    
    // separate the lines, remove a tab at the start of each, and rejoin them
    QString selectedString = cursor.selectedText();
    QRegularExpression lineEndMatch("\\R", QRegularExpression::UseUnicodePropertiesOption);
    
    return selectedString.split(lineEndMatch, QString::KeepEmptyParts);
}

void QtSLiMScriptTextEdit::shiftSelectionLeft(void)
{
     if (isEnabled() && !isReadOnly())
	{
        QTextCursor &&cursor = textCursor();
        bool movedBack;
        QStringList lines = linesForRoundedSelection(cursor, movedBack);
        
        for (QString &line : lines)
            if (line[0] == '\t')
                line.remove(0, 1);
        
        QString replacementString = lines.join(QChar::ParagraphSeparator);
		
        // end the editing block, producing one undo-able operation
        cursor.insertText(replacementString);
        cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::MoveAnchor, replacementString.length());
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, replacementString.length());
        if (movedBack)
            cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
		cursor.endEditBlock();
        setTextCursor(cursor);
	}
	else
	{
		qApp->beep();
	}
}

void QtSLiMScriptTextEdit::shiftSelectionRight(void)
{
    if (isEnabled() && !isReadOnly())
	{
        QTextCursor &&cursor = textCursor();
        bool movedBack;
        QStringList lines = linesForRoundedSelection(cursor, movedBack);
        
        for (QString &line : lines)
            line.insert(0, '\t');
        
        QString replacementString = lines.join(QChar::ParagraphSeparator);
		
        // end the editing block, producing one undo-able operation
        cursor.insertText(replacementString);
        cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::MoveAnchor, replacementString.length());
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, replacementString.length());
        if (movedBack)
            cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
		cursor.endEditBlock();
        setTextCursor(cursor);
	}
	else
	{
		qApp->beep();
	}
}

void QtSLiMScriptTextEdit::commentUncommentSelection(void)
{
    if (isEnabled() && !isReadOnly())
	{
        QTextCursor &&cursor = textCursor();
        bool movedBack;
        QStringList lines = linesForRoundedSelection(cursor, movedBack);
        
        // decide whether we are commenting or uncommenting; we are only uncommenting if every line spanned by the selection starts with "//"
		bool uncommenting = true;
        
        for (QString &line : lines)
        {
            if (!line.startsWith("//"))
            {
                uncommenting = false;
                break;
            }
        }
        
        // now do the comment / uncomment
        if (uncommenting)
        {
            for (QString &line : lines)
                line.remove(0, 2);
        }
        else
        {
            for (QString &line : lines)
                line.insert(0, "//");
        }
        
        QString replacementString = lines.join(QChar::ParagraphSeparator);
		
        // end the editing block, producing one undo-able operation
        cursor.insertText(replacementString);
        cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::MoveAnchor, replacementString.length());
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, replacementString.length());
        if (movedBack)
            cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
		cursor.endEditBlock();
        setTextCursor(cursor);
	}
	else
	{
		qApp->beep();
	}
}
































