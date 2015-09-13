//
//  EidosHelpController.h
//  SLiM
//
//  Created by Ben Haller on 9/12/15.
//  Copyright (c) 2015 Philipp Messer.  All rights reserved.
//	A product of the Messer Lab, http://messerlab.org/software/
//

//	This file is part of Eidos.
//
//	Eidos is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
//
//	Eidos is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along with Eidos.  If not, see <http://www.gnu.org/licenses/>.


#import <Cocoa/Cocoa.h>


@interface EidosHelpController : NSObject
{
}

+ (EidosHelpController *)sharedController;

- (void)showWindow;

- (void)addTopicsFromRTFFile:(NSString *)rtfFile underHeading:(NSString *)topLevelHeading;

@end






















































