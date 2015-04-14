//
//  AppDelegate.m
//  SLiMscribe
//
//  Created by Ben Haller on 4/7/15.
//  Copyright (c) 2015 Philipp Messer.  All rights reserved.
//	A product of the Messer Lab, http://messerlab.org/software/
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


#import "AppDelegate.h"
#include "script_functions.h"
#include "script_test.h"

#include <stdexcept>


using std::string;
using std::vector;
using std::map;
using std::endl;
using std::istringstream;
using std::ostringstream;
using std::istream;
using std::ostream;


// User defaults keys
NSString *defaultsShowTokensKey = @"ShowTokens";
NSString *defaultsShowParseKey = @"ShowParse";
NSString *defaultsShowExecutionKey = @"ShowExecution";


@implementation AppDelegate

@synthesize scriptWindow, mainSplitView, scriptTextView, outputTextView;

+ (void)initialize
{
	[[NSUserDefaults standardUserDefaults] registerDefaults:[NSDictionary dictionaryWithObjectsAndKeys:
															 [NSNumber numberWithBool:NO], defaultsShowTokensKey,
															 [NSNumber numberWithBool:NO], defaultsShowParseKey,
															 [NSNumber numberWithBool:NO], defaultsShowExecutionKey,
															 nil]];
}

- (void)dealloc
{
	delete global_symbols;
	global_symbols = nil;
	
	[super dealloc];
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
	// Run startup tests, if enabled
	RunSLiMScriptTests();
	
	// Tell Cocoa that we can go full-screen
	[scriptWindow setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];
	
	// Turn off all automatic substitution stuff, etc.; turning them off in the nib doesn't seem to actually turn them off, on 10.10.1
	[scriptTextView setAutomaticDashSubstitutionEnabled:NO];
	[scriptTextView setAutomaticDataDetectionEnabled:NO];
	[scriptTextView setAutomaticLinkDetectionEnabled:NO];
	[scriptTextView setAutomaticQuoteSubstitutionEnabled:NO];
	[scriptTextView setAutomaticSpellingCorrectionEnabled:NO];
	[scriptTextView setAutomaticTextReplacementEnabled:NO];
	[scriptTextView setContinuousSpellCheckingEnabled:NO];
	[scriptTextView setGrammarCheckingEnabled:NO];
	[scriptTextView turnOffLigatures:nil];
	
	[outputTextView setAutomaticDashSubstitutionEnabled:NO];
	[outputTextView setAutomaticDataDetectionEnabled:NO];
	[outputTextView setAutomaticLinkDetectionEnabled:NO];
	[outputTextView setAutomaticQuoteSubstitutionEnabled:NO];
	[outputTextView setAutomaticSpellingCorrectionEnabled:NO];
	[outputTextView setAutomaticTextReplacementEnabled:NO];
	[outputTextView setContinuousSpellCheckingEnabled:NO];
	[outputTextView setGrammarCheckingEnabled:NO];
	[outputTextView turnOffLigatures:nil];
	
	// Fix textview fonts
	[scriptTextView setFont:[NSFont fontWithName:@"Menlo" size:11.0]];
	[outputTextView setFont:[NSFont fontWithName:@"Menlo" size:11.0]];
	
	// Fix text container insets to look a bit nicer; {0,0} by default
	[scriptTextView setTextContainerInset:NSMakeSize(0.0, 5.0)];
	[outputTextView setTextContainerInset:NSMakeSize(0.0, 5.0)];
	
	// Show a welcome message
	[outputTextView showWelcomeMessage];
	
	// And show our prompt
	[outputTextView showPrompt];
	
	[scriptWindow makeFirstResponder:outputTextView];
}

- (NSString *)_executeScriptString:(NSString *)scriptString tokenString:(NSString **)tokenString parseString:(NSString **)parseString executionString:(NSString **)executionString addOptionalSemicolon:(BOOL)addSemicolon
{
	string script_string([scriptString UTF8String]);
	Script script(1, 1, script_string, 0);
	ScriptValue *result;
	string output;
	
	// Tokenize
	try {
		script.Tokenize();
		
		// Add a semicolon if needed; this allows input like "6+7" in the console
		if (addSemicolon)
			script.AddOptionalSemicolon();
		
		if (tokenString)
		{
			ostringstream token_stream;
			
			script.PrintTokens(token_stream);
			
			*tokenString = [NSString stringWithUTF8String:token_stream.str().c_str()];
		}
	}
	catch (std::runtime_error err)
	{
		return [NSString stringWithUTF8String:GetUntrimmedRaiseMessage().c_str()];
	}
	
	// Parse, an "interpreter block" bounded by an EOF rather than a "script block" that requires braces
	try {
		script.ParseInterpreterBlockToAST();
		
		if (parseString)
		{
			ostringstream parse_stream;
			
			script.PrintAST(parse_stream);
			
			*parseString = [NSString stringWithUTF8String:parse_stream.str().c_str()];
		}
	}
	catch (std::runtime_error err)
	{
		return [NSString stringWithUTF8String:GetUntrimmedRaiseMessage().c_str()];
	}
	
	// Interpret the parsed block
	ScriptInterpreter interpreter(script, global_symbols);		// give the interpreter the symbol table
	global_symbols = nullptr;
	
	try {
		if (executionString)
			interpreter.SetShouldLogExecution(true);
		
		result = interpreter.EvaluateInterpreterBlock();
		output = interpreter.ExecutionOutput();
		
		global_symbols = interpreter.YieldSymbolTable();			// take the symbol table back
		
		if (executionString)
			*executionString = [NSString stringWithUTF8String:interpreter.ExecutionLog().c_str()];
	}
	catch (std::runtime_error err)
	{
		global_symbols = interpreter.YieldSymbolTable();			// take the symbol table back despite the raise
		
		return [NSString stringWithUTF8String:GetUntrimmedRaiseMessage().c_str()];
	}
	
	return [NSString stringWithUTF8String:output.c_str()];
}

- (void)executeScriptString:(NSString *)scriptString addOptionalSemicolon:(BOOL)addSemicolon
{
	NSTextStorage *ts = [outputTextView textStorage];
	NSString *tokenString = nil, *parseString = nil, *executionString = nil;
	NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
	BOOL showTokens = [defaults boolForKey:defaultsShowTokensKey];
	BOOL showParse = [defaults boolForKey:defaultsShowParseKey];
	BOOL showExecution = [defaults boolForKey:defaultsShowExecutionKey];
	
	NSString *result = [self _executeScriptString:scriptString
									  tokenString:(showTokens ? &tokenString : NULL)
									  parseString:(showParse ? &parseString : NULL)
								  executionString:(showExecution ? &executionString : NULL)
							 addOptionalSemicolon:addSemicolon];
	
	// make the attributed strings we will append
	NSDictionary *outAttrDict = ([result hasPrefix:@"ERROR"] ? [ConsoleTextView errorAttrs] : [ConsoleTextView outputAttrs]);
	NSAttributedString *outputString1 = [[NSAttributedString alloc] initWithString:@"\n" attributes:[ConsoleTextView inputAttrs]];
	NSAttributedString *outputString2 = [[NSAttributedString alloc] initWithString:result attributes:outAttrDict];
	
	NSAttributedString *tokenAttrString = nil, *parseAttrString = nil, *executionAttrString = nil;
	
	if (tokenString)
		tokenAttrString = [[NSAttributedString alloc] initWithString:tokenString attributes:[ConsoleTextView tokensAttrs]];
	if (parseString)
		parseAttrString = [[NSAttributedString alloc] initWithString:parseString attributes:[ConsoleTextView parseAttrs]];
	if (executionString)
		executionAttrString = [[NSAttributedString alloc] initWithString:executionString attributes:[ConsoleTextView executionAttrs]];
	
	// do the editing
	[ts beginEditing];
	[ts replaceCharactersInRange:NSMakeRange([ts length], 0) withAttributedString:outputString1];
	[outputTextView appendSpacer];
	 
	if (tokenAttrString)
	{
		[ts replaceCharactersInRange:NSMakeRange([ts length], 0) withAttributedString:tokenAttrString];
		[outputTextView appendSpacer];
	}
	
	if (parseAttrString)
	{
		[ts replaceCharactersInRange:NSMakeRange([ts length], 0) withAttributedString:parseAttrString];
		[outputTextView appendSpacer];
	}
	
	if (executionAttrString)
	{
		[ts replaceCharactersInRange:NSMakeRange([ts length], 0) withAttributedString:executionAttrString];
		[outputTextView appendSpacer];
	}
	
	[ts replaceCharactersInRange:NSMakeRange([ts length], 0) withAttributedString:outputString2];
	[outputTextView appendSpacer];
	
	[ts endEditing];
	
	// clean up
	[outputString1 release];
	[tokenAttrString release];
	[parseAttrString release];
	[executionAttrString release];
	[outputString2 release];
	
	// and show a new prompt
	[outputTextView showPrompt];
}


//
//	Actions
//
#pragma mark Actions

- (IBAction)showAboutWindow:(id)sender
{
	[[NSBundle mainBundle] loadNibNamed:@"AboutWindow" owner:self topLevelObjects:NULL];
	
	// The window is the top-level object in this nib.  It will release itself when closed, so we will retain it on its behalf here.
	// Note that the aboutWindow and aboutWebView outlets do not get zeroed out when the about window closes; but we only use them here.
	[aboutWindow retain];
	
	// Set our version number string
	NSString *bundleVersion = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
	NSString *versionString = [NSString stringWithFormat:@"v. %@", bundleVersion];
	
	[aboutVersionTextField setStringValue:versionString];
	
	// Now that everything is set up, show the window
	[aboutWindow makeKeyAndOrderFront:nil];
}

- (IBAction)sendFeedback:(id)sender
{
	[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"mailto:philipp.messer@gmail.com?subject=SLiM%20Feedback"]];
}

- (IBAction)showMesserLab:(id)sender
{
	[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"http://messerlab.org/"]];
}

- (IBAction)showBenHaller:(id)sender
{
	[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"http://www.benhaller.com/"]];
}

- (IBAction)showStickSoftware:(id)sender
{
	[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"http://www.sticksoftware.com/"]];
}

- (IBAction)checkScript:(id)sender
{
	NSLog(@"checkScript:");
}

- (IBAction)showScriptHelp:(id)sender
{
	NSTextStorage *ts = [outputTextView textStorage];
	NSAttributedString *scriptAttrString = [[NSAttributedString alloc] initWithString:@"help()" attributes:[ConsoleTextView inputAttrs]];
	NSUInteger promptEnd = [outputTextView promptRangeEnd];
	
	[ts beginEditing];
	[ts replaceCharactersInRange:NSMakeRange(promptEnd, [ts length] - promptEnd) withAttributedString:scriptAttrString];
	[ts endEditing];
	
	[outputTextView registerNewHistoryItem:@"help()"];
	[self executeScriptString:@"help()" addOptionalSemicolon:YES];
}

- (IBAction)clearOutput:(id)sender
{
	[outputTextView clearOutput];
}

- (IBAction)executeAll:(id)sender
{
	NSTextStorage *ts = [outputTextView textStorage];
	NSString *fullScriptString = [scriptTextView string];
	NSArray *scriptLines = [fullScriptString componentsSeparatedByString:@"\n"];
	NSString *scriptString = [scriptLines componentsJoinedByString:@"\n  "];	// add spaces the match the prompt indent
	NSAttributedString *scriptAttrString = [[NSAttributedString alloc] initWithString:scriptString attributes:[ConsoleTextView inputAttrs]];
	NSUInteger promptEnd = [outputTextView promptRangeEnd];
	
	[ts beginEditing];
	[ts replaceCharactersInRange:NSMakeRange(promptEnd, [ts length] - promptEnd) withAttributedString:scriptAttrString];
	[ts endEditing];
	
	[outputTextView registerNewHistoryItem:scriptString];
	[self executeScriptString:scriptString addOptionalSemicolon:NO];
}

- (IBAction)executeSelection:(id)sender
{
	NSTextStorage *ts = [outputTextView textStorage];
	NSString *fullScriptString = [scriptTextView string];
	NSArray *scriptLines = [fullScriptString componentsSeparatedByString:@"\n"];
	NSRange selectedRange = [scriptTextView selectedRange];
	NSMutableArray *selectedLines = [NSMutableArray array];
	NSUInteger lengthSoFar = 0;
	
	for (NSString *line in scriptLines)
	{
		NSUInteger lineLength = [line length];
		NSRange lineRange = NSMakeRange(lengthSoFar, lineLength);
		
		if ((selectedRange.location <= lineRange.location + lineRange.length) && (selectedRange.location + selectedRange.length >= lineRange.location))
			[selectedLines addObject:line];
		
		lengthSoFar += lineLength + 1;
	}
	
	NSString *scriptString = [selectedLines componentsJoinedByString:@"\n  "];	// add spaces the match the prompt indent
	NSAttributedString *scriptAttrString = [[NSAttributedString alloc] initWithString:scriptString attributes:[ConsoleTextView inputAttrs]];
	NSUInteger promptEnd = [outputTextView promptRangeEnd];
	
	[ts beginEditing];
	[ts replaceCharactersInRange:NSMakeRange(promptEnd, [ts length] - promptEnd) withAttributedString:scriptAttrString];
	[ts endEditing];
	
	[outputTextView registerNewHistoryItem:scriptString];
	[self executeScriptString:scriptString addOptionalSemicolon:NO];
}


//
//	NSTextView delegate methods
//
#pragma mark NSTextView delegate

- (NSRange)textView:(NSTextView *)textView willChangeSelectionFromCharacterRange:(NSRange)oldRange toCharacterRange:(NSRange)newRange
{
	if (textView == outputTextView)
	{
		// prevent a zero-length selection (i.e. an insertion point) in the history
		if ((newRange.length == 0) && (newRange.location < [outputTextView promptRangeEnd]))
			return NSMakeRange([[outputTextView string] length], 0);
	}
	
	return newRange;
}

- (BOOL)textView:(NSTextView *)textView shouldChangeTextInRange:(NSRange)affectedCharRange replacementString:(NSString *)replacementString
{
	if (textView == outputTextView)
	{
		// prevent the user from changing anything above the current prompt
		if (affectedCharRange.location < [outputTextView promptRangeEnd])
			return NO;
	}
	
	return YES;
}

- (void)executeConsoleInput:(ConsoleTextView *)textView
{
	if (textView == outputTextView)
	{
		NSString *outputString = [outputTextView string];
		NSString *scriptString = [outputString substringFromIndex:[outputTextView promptRangeEnd]];
		
		[outputTextView registerNewHistoryItem:scriptString];
		[self executeScriptString:scriptString addOptionalSemicolon:YES];
	}
}


//
//	NSWindow delegate methods
//
#pragma mark NSWindow delegate

- (void)windowWillClose:(NSNotification *)notification
{
	NSWindow *closingWindow = [notification object];
	
	if (closingWindow == scriptWindow)
	{
		[scriptWindow setDelegate:nil];
		[scriptWindow release];
		scriptWindow = nil;
		
		[[NSApplication sharedApplication] terminate:nil];
	}
}


//
//	NSSplitView delegate methods
//
#pragma mark NSSplitView delegate

- (BOOL)splitView:(NSSplitView *)splitView canCollapseSubview:(NSView *)subview
{
	return YES;
}

- (CGFloat)splitView:(NSSplitView *)splitView constrainMaxCoordinate:(CGFloat)proposedMax ofSubviewAt:(NSInteger)dividerIndex
{
	return proposedMax - 200;
}

- (CGFloat)splitView:(NSSplitView *)splitView constrainMinCoordinate:(CGFloat)proposedMin ofSubviewAt:(NSInteger)dividerIndex
{
	return proposedMin + 200;
}

@end


































































