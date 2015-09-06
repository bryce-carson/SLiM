//
//  EidosValueWrapper.m
//  EidosScribe
//
//  Created by Ben Haller on 5/31/15.
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


#import "EidosValueWrapper.h"

#include "eidos_value.h"
#include "eidos_property_signature.h"


@implementation EidosValueWrapper

+ (instancetype)wrapperForName:(NSString *)aName parent:(EidosValueWrapper *)parent value:(EidosValue *)aValue
{
	return [[[self alloc] initWithWrappedName:aName parent:parent value:aValue index:-1] autorelease];
}

+ (instancetype)wrapperForName:(NSString *)aName parent:(EidosValueWrapper *)parent value:(EidosValue *)aValue index:(int)anIndex
{
	return [[[self alloc] initWithWrappedName:aName parent:parent value:aValue index:anIndex] autorelease];
}

- (instancetype)initWithWrappedName:(NSString *)aName parent:(EidosValueWrapper *)parent value:(EidosValue *)aValue index:(int)anIndex
{
	if (self = [super init])
	{
		parentWrapper = [parent retain];
		
		wrappedName = [aName retain];
		wrappedIndex = anIndex;
		
		wrappedValue = aValue;
		valueIsOurs = wrappedValue->IsTemporary();
		isExpandable = (wrappedValue->Type() == EidosValueType::kValueObject);
		
		childWrappers = nil;
	}
	
	return self;
}

- (void)dealloc
{
	// At the point that dealloc gets called, the value object that we wrap may already be gone.  This happens if
	// the Context is responsible for the object and something happened to make the object go away.  We therefore
	// can't touch the value pointer at all here unless we own it.  This is why we mark the ones that we own ahead
	// of time, instead of just checking the IsTemporary() flag here.
	
	if (valueIsOurs && (wrappedIndex == -1))
		delete wrappedValue;
	
	wrappedValue = nullptr;
	valueIsOurs = false;
	
	[wrappedName release];
	wrappedName = nil;
	
	[parentWrapper release];
	parentWrapper = nil;
	
	[childWrappers release];
	childWrappers = nil;
	
	[super dealloc];
}

- (void)recacheWrappers
{
	[childWrappers release];
	childWrappers = [NSMutableArray new];
	
	int elementCount = wrappedValue->Count();
	
	// values which are of object type and contain more than one element get displayed as a list of elements
	if (elementCount > 1)
	{
		for (int index = 0; index < elementCount;++ index)
		{
			NSString *childName = [NSString stringWithFormat:@"%@[%ld]", wrappedName, (long)index];
			EidosValue *childValue = wrappedValue->GetValueAtIndex(index, nullptr);
			EidosValueWrapper *childWrapper = [EidosValueWrapper wrapperForName:childName parent:self value:childValue index:index];
			
			[childWrappers addObject:childWrapper];
		}
	}
	else if (wrappedValue->Type() == EidosValueType::kValueObject)
	{
		EidosValue_Object *wrapped_object = ((EidosValue_Object *)wrappedValue);
		const EidosObjectClass *object_class = wrapped_object->Class();
		const std::vector<const EidosPropertySignature *> *properties = object_class->Properties();
		int propertyCount = (int)properties->size();
		
		for (int index = 0; index < propertyCount; ++index)
		{
			const EidosPropertySignature *propertySig = (*properties)[index];
			const std::string &symbolName = propertySig->property_name_;
			EidosGlobalStringID symbolID = propertySig->property_id_;
			EidosValue *symbolValue = wrapped_object->GetPropertyOfElements(symbolID);
			NSString *symbolObjcName = [NSString stringWithUTF8String:symbolName.c_str()];
			EidosValueWrapper *childWrapper = [EidosValueWrapper wrapperForName:symbolObjcName parent:self value:symbolValue];
			
			[childWrappers addObject:childWrapper];
		}
	}
}

- (void)invalidateWrappedValues
{
	if (valueIsOurs && (wrappedIndex == -1))
		delete wrappedValue;
	
	wrappedValue = nullptr;
	valueIsOurs = false;
	
	[childWrappers makeObjectsPerformSelector:@selector(invalidateWrappedValues)];
}

- (void)releaseChildWrappers
{
	[childWrappers makeObjectsPerformSelector:@selector(releaseChildWrappers)];
	
	[childWrappers release];
	childWrappers = nil;
}

- (BOOL)isEqual:(id)anObject
{
	if (self == anObject)
		return YES;
	if (![anObject isMemberOfClass:[EidosValueWrapper class]])
		return NO;
	
	EidosValueWrapper *otherWrapper = (EidosValueWrapper *)anObject;
	
	if (wrappedIndex != otherWrapper->wrappedIndex)
		return NO;
	if (![wrappedName isEqualToString:otherWrapper->wrappedName])
		return NO;
	
	// We call through to isEqualToWrapper: only if the parent wrapper is defined for both objects;
	// this is partly for speed, and partly because [nil isEqualToWrapper:nil] would give NO.
	EidosValueWrapper *otherWrapperParent = otherWrapper->parentWrapper;
	
	if (parentWrapper == otherWrapperParent)
		return YES;
	if ((parentWrapper && !otherWrapperParent) || (!parentWrapper && otherWrapperParent))
		return NO;
	
	if (![parentWrapper isEqualToWrapper:otherWrapperParent])
		return NO;
	
	return YES;
}

- (BOOL)isEqualToWrapper:(EidosValueWrapper *)otherWrapper
{
	// Note that this method is missing the self==object test at the beginning!  This is because it
	// was already done by the caller; this method is not designed to be called by arbitrary caller!
	// Similarly, it does not check for object==nil, so it will crash if called with nil!
	
	if (wrappedIndex != otherWrapper->wrappedIndex)
		return NO;
	if (![wrappedName isEqualToString:otherWrapper->wrappedName])
		return NO;
	
	// We call through to isEqualToWrapper: only if the parent wrapper is defined for both objects;
	// this is partly for speed, and partly because [nil isEqualToWrapper:nil] would give NO.
	EidosValueWrapper *otherWrapperParent = otherWrapper->parentWrapper;
	
	if (parentWrapper == otherWrapperParent)
		return YES;
	if ((parentWrapper && !otherWrapperParent) || (!parentWrapper && otherWrapperParent))
		return NO;
	
	if (![parentWrapper isEqualToWrapper:otherWrapperParent])
		return NO;
	
	return YES;
}

- (NSUInteger)hash
{
	NSUInteger hash = [wrappedName hash];
	
	hash ^= wrappedIndex;
	
	if (parentWrapper)
		hash ^= ([parentWrapper hash] << 1);
	
	//NSLog(@"hash for item named %@ == %lu", wrappedName, (unsigned long)hash);
	
	return hash;
}

@end



















































