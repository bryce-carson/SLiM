//
//  subpopulation.cpp
//  SLiM
//
//  Created by Ben Haller on 12/13/14.
//  Copyright (c) 2014 Philipp Messer.  All rights reserved.
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


#include <iostream>
#include <assert.h>

#include "subpopulation.h"
#include "slim_sim.h"
#include "slim_global.h"
#include "eidos_call_signature.h"
#include "eidos_property_signature.h"


using std::string;
using std::endl;


// given the subpop size and sex ratio currently set for the child generation, make new genomes to fit
void Subpopulation::GenerateChildrenToFit(const bool p_parents_also)
{
#ifdef DEBUG
	bool old_log = Genome::LogGenomeCopyAndAssign(false);
#endif
	
	// throw out whatever used to be there
	child_genomes_.clear();
	if (p_parents_also)
		parent_genomes_.clear();
	
	// make new stuff
	if (sex_enabled_)
	{
		// Figure out the first male index from the sex ratio, and exit if we end up with all one sex
		child_first_male_index_ = static_cast<int>(lround((1.0 - child_sex_ratio_) * child_subpop_size_));
		
		if (child_first_male_index_ <= 0)
			EIDOS_TERMINATION << "ERROR (GenerateChildrenToFit): child sex ratio of " << child_sex_ratio_ << " produced no females" << eidos_terminate();
		else if (child_first_male_index_ >= child_subpop_size_)
			EIDOS_TERMINATION << "ERROR (GenerateChildrenToFit): child sex ratio of " << child_sex_ratio_ << " produced no males" << eidos_terminate();
		
		if (p_parents_also)
		{
			parent_first_male_index_ = static_cast<int>(lround((1.0 - parent_sex_ratio_) * parent_subpop_size_));
			
			if (parent_first_male_index_ <= 0)
				EIDOS_TERMINATION << "ERROR (GenerateChildrenToFit): parent sex ratio of " << parent_sex_ratio_ << " produced no females" << eidos_terminate();
			else if (parent_first_male_index_ >= parent_subpop_size_)
				EIDOS_TERMINATION << "ERROR (GenerateChildrenToFit): parent sex ratio of " << parent_sex_ratio_ << " produced no males" << eidos_terminate();
		}
		
		switch (modeled_chromosome_type_)
		{
			case GenomeType::kAutosome:
				// produces default Genome objects of type GenomeType::kAutosome
				child_genomes_.resize(2 * child_subpop_size_);
				if (p_parents_also)
					parent_genomes_.resize(2 * parent_subpop_size_);
				break;
			case GenomeType::kXChromosome:
			case GenomeType::kYChromosome:
			{
				// if we are not modeling a given chromosome type, then instances of it are null – they will log and exit if used
				Genome x_model = Genome(GenomeType::kXChromosome, modeled_chromosome_type_ != GenomeType::kXChromosome);
				Genome y_model = Genome(GenomeType::kYChromosome, modeled_chromosome_type_ != GenomeType::kYChromosome);
				
				child_genomes_.reserve(2 * child_subpop_size_);
				
				// females get two Xs
				for (int i = 0; i < child_first_male_index_; ++i)
				{
					child_genomes_.push_back(x_model);
					child_genomes_.push_back(x_model);
				}
				
				// males get an X and a Y
				for (int i = child_first_male_index_; i < child_subpop_size_; ++i)
				{
					child_genomes_.push_back(x_model);
					child_genomes_.push_back(y_model);
				}
				
				if (p_parents_also)
				{
					parent_genomes_.reserve(2 * parent_subpop_size_);
					
					// females get two Xs
					for (int i = 0; i < parent_first_male_index_; ++i)
					{
						parent_genomes_.push_back(x_model);
						parent_genomes_.push_back(x_model);
					}
					
					// males get an X and a Y
					for (int i = parent_first_male_index_; i < parent_subpop_size_; ++i)
					{
						parent_genomes_.push_back(x_model);
						parent_genomes_.push_back(y_model);
					}
				}
				break;
			}
		}
	}
	else
	{
		// produces default Genome objects of type GenomeType::kAutosome
		child_genomes_.resize(2 * child_subpop_size_);
		if (p_parents_also)
			parent_genomes_.resize(2 * parent_subpop_size_);
	}
	
#ifdef DEBUG
	Genome::LogGenomeCopyAndAssign(old_log);
#endif
}

Subpopulation::Subpopulation(Population &p_population, int p_subpopulation_id, int p_subpop_size) : population_(p_population), subpopulation_id_(p_subpopulation_id), parent_subpop_size_(p_subpop_size), child_subpop_size_(p_subpop_size)
{
	GenerateChildrenToFit(true);
	
	// Set up to draw random individuals, based initially on equal fitnesses
	cached_parental_fitness_ = (double *)realloc(cached_parental_fitness_, sizeof(double) * parent_subpop_size_);
	cached_fitness_capacity_ = parent_subpop_size_;
	cached_fitness_size_ = parent_subpop_size_;
	
	double *fitness_buffer_ptr = cached_parental_fitness_;
	
	for (int i = 0; i < parent_subpop_size_; i++)
		*(fitness_buffer_ptr++) = 1.0;
	
	lookup_parent_ = gsl_ran_discrete_preproc(parent_subpop_size_, cached_parental_fitness_);
}

// SEX ONLY
Subpopulation::Subpopulation(Population &p_population, int p_subpopulation_id, int p_subpop_size, double p_sex_ratio, GenomeType p_modeled_chromosome_type, double p_x_chromosome_dominance_coeff) :
population_(p_population), subpopulation_id_(p_subpopulation_id), sex_enabled_(true), parent_subpop_size_(p_subpop_size), child_subpop_size_(p_subpop_size), parent_sex_ratio_(p_sex_ratio), child_sex_ratio_(p_sex_ratio), modeled_chromosome_type_(p_modeled_chromosome_type), x_chromosome_dominance_coeff_(p_x_chromosome_dominance_coeff)
{
	GenerateChildrenToFit(true);
	
	// Set up to draw random females, based initially on equal fitnesses
	cached_parental_fitness_ = (double *)realloc(cached_parental_fitness_, sizeof(double) * parent_subpop_size_);
	cached_male_fitness_ = (double *)realloc(cached_male_fitness_, sizeof(double) * parent_subpop_size_);
	cached_fitness_capacity_ = parent_subpop_size_;
	cached_fitness_size_ = parent_subpop_size_;
	
	double *fitness_buffer_ptr = cached_parental_fitness_;
	double *male_buffer_ptr = cached_male_fitness_;
	
	for (int i = 0; i < parent_subpop_size_; i++)
	{
		*(fitness_buffer_ptr++) = 1.0;
		*(male_buffer_ptr++) = 0.0;				// this vector has 0 for all females, for mateChoice() callbacks
	}
	
	// Set up to draw random males, based initially on equal fitnesses
	int num_males = parent_subpop_size_ - parent_first_male_index_;
	
	for (int i = 0; i < num_males; i++)
	{
		*(fitness_buffer_ptr++) = 1.0;
		*(male_buffer_ptr++) = 1.0;
	}
	
	lookup_female_parent_ = gsl_ran_discrete_preproc(parent_first_male_index_, cached_parental_fitness_);
	lookup_male_parent_ = gsl_ran_discrete_preproc(num_males, cached_parental_fitness_ + parent_first_male_index_);
}


Subpopulation::~Subpopulation(void)
{
	//EIDOS_ERRSTREAM << "Subpopulation::~Subpopulation" << std::endl;
	
	gsl_ran_discrete_free(lookup_parent_);
	gsl_ran_discrete_free(lookup_female_parent_);
	gsl_ran_discrete_free(lookup_male_parent_);
	
	if (self_symbol_)
	{
		delete self_symbol_->second;
		delete self_symbol_;
	}
	
	if (cached_value_subpop_id_)
		delete cached_value_subpop_id_;
	
	if (cached_parental_fitness_)
		free(cached_parental_fitness_);
	
	if (cached_male_fitness_)
		free(cached_male_fitness_);
}

void Subpopulation::UpdateFitness(std::vector<SLiMEidosBlock*> &p_fitness_callbacks)
{
#ifdef SLIMGUI
	// When running under SLiMgui, this function calculates the population mean fitness as a side effect
	double totalFitness = 0.0;
#endif
	
	// We cache the calculated fitness values, for use in PopulationView and mateChoice() callbacks and such
	if (cached_fitness_capacity_ < parent_subpop_size_)
	{
		cached_parental_fitness_ = (double *)realloc(cached_parental_fitness_, sizeof(double) * parent_subpop_size_);
		if (sex_enabled_)
			cached_male_fitness_ = (double *)realloc(cached_male_fitness_, sizeof(double) * parent_subpop_size_);
		cached_fitness_capacity_ = parent_subpop_size_;
	}
	
	cached_fitness_size_ = 0;	// while we're refilling, the fitness cache is invalid
	
	// calculate fitnesses in parent population and create new lookup table
	if (sex_enabled_)
	{
		// SEX ONLY
		gsl_ran_discrete_free(lookup_female_parent_);
		gsl_ran_discrete_free(lookup_male_parent_);
		
		// Set up to draw random females
		for (int i = 0; i < parent_first_male_index_; i++)
		{
			double fitness = FitnessOfParentWithGenomeIndices(2 * i, 2 * i + 1, p_fitness_callbacks);
			
			cached_parental_fitness_[i] = fitness;
			cached_male_fitness_[i] = 0;				// this vector has 0 for all females, for mateChoice() callbacks
			
#ifdef SLIMGUI
			totalFitness += fitness;
#endif
		}
		
		lookup_female_parent_ = gsl_ran_discrete_preproc(parent_first_male_index_, cached_parental_fitness_);
		
		// Set up to draw random males
		int num_males = parent_subpop_size_ - parent_first_male_index_;
		
		for (int i = 0; i < num_males; i++)
		{
			int individual_index = (i + parent_first_male_index_);
			double fitness = FitnessOfParentWithGenomeIndices(2 * individual_index, 2 * individual_index + 1, p_fitness_callbacks);
			
			cached_parental_fitness_[individual_index] = fitness;
			cached_male_fitness_[individual_index] = fitness;
			
#ifdef SLIMGUI
			totalFitness += fitness;
#endif
		}
		
		lookup_male_parent_ = gsl_ran_discrete_preproc(num_males, cached_parental_fitness_ + parent_first_male_index_);
	}
	else
	{
		double *fitness_buffer_ptr = cached_parental_fitness_;
		
		gsl_ran_discrete_free(lookup_parent_);
		
		for (int i = 0; i < parent_subpop_size_; i++)
		{
			double fitness = FitnessOfParentWithGenomeIndices(2 * i, 2 * i + 1, p_fitness_callbacks);
			
			*(fitness_buffer_ptr++) = fitness;
			
#ifdef SLIMGUI
			totalFitness += fitness;
#endif
		}
		
		lookup_parent_ = gsl_ran_discrete_preproc(parent_subpop_size_, cached_parental_fitness_);
	}
	
	cached_fitness_size_ = parent_subpop_size_;
	
#ifdef SLIMGUI
	parental_total_fitness_ = totalFitness;
#endif
}

double Subpopulation::ApplyFitnessCallbacks(Mutation *p_mutation, int p_homozygous, double p_computed_fitness, std::vector<SLiMEidosBlock*> &p_fitness_callbacks, Genome *genome1, Genome *genome2)
{
	int mutation_type_id = p_mutation->mutation_type_ptr_->mutation_type_id_;
	SLiMSim &sim = population_.sim_;
	
	for (SLiMEidosBlock *fitness_callback : p_fitness_callbacks)
	{
		if (fitness_callback->active_)
		{
			int callback_mutation_type_id = fitness_callback->mutation_type_id_;
			
			if ((callback_mutation_type_id == -1) || (callback_mutation_type_id == mutation_type_id))
			{
				// The callback is active and matches the mutation type id of the mutation, so we need to execute it
				// This code is similar to Population::ExecuteScript, but we inject some additional values, and we use the return value
				const EidosASTNode *compound_statement_node = fitness_callback->compound_statement_node_;
				
				if (compound_statement_node->cached_value_)
				{
					// The script is a constant expression such as "{ return 1.1; }", so we can short-circuit it completely
					EidosValue *result = compound_statement_node->cached_value_;
					
					if ((result->Type() != EidosValueType::kValueFloat) || (result->Count() != 1))
						EIDOS_TERMINATION << "ERROR (ApplyFitnessCallbacksToFitness): fitness() callbacks must provide a float singleton return value." << eidos_terminate();
					
					p_computed_fitness = result->FloatAtIndex(0);
					
					// the cached value is owned by the tree, so we do not dispose of it
					// there is also no script output to handle
				}
				else
				{
					// local variables for the callback parameters that we might need to allocate here, and thus need to free below
					EidosValue_Object_singleton_const local_mut(p_mutation);
					EidosValue_Float_singleton_const local_relFitness(p_computed_fitness);
					
					// We need to actually execute the script; we start a block here to manage the lifetime of the symbol table
					{
						EidosSymbolTable global_symbols(&fitness_callback->eidos_contains_);
						EidosInterpreter interpreter(fitness_callback->compound_statement_node_, global_symbols);
						
						sim.InjectIntoInterpreter(interpreter, fitness_callback, true);
						
						// Set all of the callback's parameters; note we use InitializeConstantSymbolEntry() for speed.
						// We can use that method because we know the lifetime of the symbol table is shorter than that of
						// the value objects, and we know that the values we are setting here will not change (the objects
						// referred to by the values may change, but the values themselves will not change).
						if (fitness_callback->contains_mut_)
						{
							local_mut.SetExternalPermanent();
							global_symbols.InitializeConstantSymbolEntry(gStr_mut, &local_mut);
						}
						if (fitness_callback->contains_relFitness_)
						{
							local_relFitness.SetExternalPermanent();
							global_symbols.InitializeConstantSymbolEntry(gStr_relFitness, &local_relFitness);
						}
						if (fitness_callback->contains_genome1_)
							global_symbols.InitializeConstantSymbolEntry(gStr_genome1, genome1->CachedEidosValue());
						if (fitness_callback->contains_genome2_)
							global_symbols.InitializeConstantSymbolEntry(gStr_genome2, genome2->CachedEidosValue());
						if (fitness_callback->contains_subpop_)
							global_symbols.InitializeConstantSymbolEntry(gStr_subpop, CachedSymbolTableEntry()->second);
						
						// p_homozygous == -1 means the mutation is opposed by a NULL chromosome; otherwise, 0 means heterozyg., 1 means homozyg.
						// that gets translated into Eidos values of NULL, F, and T, respectively
						if (fitness_callback->contains_homozygous_)
						{
							if (p_homozygous == -1)
								global_symbols.InitializeConstantSymbolEntry(gStr_homozygous, gStaticEidosValueNULL);
							else
								global_symbols.InitializeConstantSymbolEntry(gStr_homozygous, (p_homozygous != 0) ? gStaticEidosValue_LogicalT : gStaticEidosValue_LogicalF);
						}
						
						// Interpret the script; the result from the interpretation must be a singleton double used as a new fitness value
						EidosValue *result = interpreter.EvaluateInternalBlock();
						
						if ((result->Type() != EidosValueType::kValueFloat) || (result->Count() != 1))
							EIDOS_TERMINATION << "ERROR (ApplyFitnessCallbacksToFitness): fitness() callbacks must provide a float singleton return value." << eidos_terminate();
						
						p_computed_fitness = result->FloatAtIndex(0);
						
						if (result->IsTemporary()) delete result;
						
						// Output generated by the interpreter goes to our output stream
						std::string &&output_string = interpreter.ExecutionOutput();
						
						if (!output_string.empty())
							EIDOS_OUTSTREAM << output_string;
					}
				}
			}
		}
	}
	
	return p_computed_fitness;
}

double Subpopulation::FitnessOfParentWithGenomeIndices(int p_genome_index1, int p_genome_index2, std::vector<SLiMEidosBlock*> &p_fitness_callbacks)
{
	// Are any fitness() callbacks active this generation?  Unfortunately, all the code below is bifurcated based on this flag.  This is for two reasons.  First,
	// it allows the case without fitness() callbacks to run at full speed; testing the flag each time around the loop through the mutations in the genomes
	// would presumably result in a large speed penalty.  Second, the non-callback case short-circuits when the selection coefficient is exactly 0.0f, as an
	// optimization; but that optimization would be invalid in the callback case, since callbacks can change the relative fitness of ostensibly neutral
	// mutations.  For reasons of maintainability, the branches on fitness_callbacks_exist below should be kept in synch as closely as possible.
	bool fitness_callbacks_exist = (p_fitness_callbacks.size() > 0);
	
	// calculate the fitness of the individual constituted by genome1 and genome2 in the parent population
	double w = 1.0;
	
	Genome *genome1 = &(parent_genomes_[p_genome_index1]);
	Genome *genome2 = &(parent_genomes_[p_genome_index2]);
	bool genome1_null = genome1->IsNull();
	bool genome2_null = genome2->IsNull();
	
	if (genome1_null && genome2_null)
	{
		// SEX ONLY: both genomes are placeholders; for example, we might be simulating the Y chromosome, and this is a female
		return w;
	}
	else if (genome1_null || genome2_null)
	{
		// SEX ONLY: one genome is null, so we just need to scan through the modeled genome and account for its mutations, including the x-dominance coefficient
		const Genome *genome = genome1_null ? genome2 : genome1;
		Mutation **genome_iter = genome->begin_pointer();
		Mutation **genome_max = genome->end_pointer();
		
		if (genome->GenomeType() == GenomeType::kXChromosome)
		{
			// with an unpaired X chromosome, we need to multiply each selection coefficient by the X chromosome dominance coefficient
			if (fitness_callbacks_exist)
			{
				while (genome_iter != genome_max)
				{
					Mutation *genome_mutation = *genome_iter;
					float selection_coeff = genome_mutation->selection_coeff_;
					double rel_fitness = (1.0 + x_chromosome_dominance_coeff_ * selection_coeff);
					
					w *= ApplyFitnessCallbacks(genome_mutation, -1, rel_fitness, p_fitness_callbacks, genome1, genome2);
					
					if (w <= 0.0)
						return 0.0;
					
					genome_iter++;
				}
			}
			else
			{
				while (genome_iter != genome_max)
				{
					const Mutation *genome_mutation = *genome_iter;
					float selection_coeff = genome_mutation->selection_coeff_;
					
					if (selection_coeff != 0.0f)
					{
						w *= (1.0 + x_chromosome_dominance_coeff_ * selection_coeff);
						
						if (w <= 0.0)
							return 0.0;
					}
					
					genome_iter++;
				}
			}
		}
		else
		{
			// with other types of unpaired chromosomes (like the Y chromosome of a male when we are modeling the Y) there is no dominance coefficient
			if (fitness_callbacks_exist)
			{
				while (genome_iter != genome_max)
				{
					Mutation *genome_mutation = *genome_iter;
					float selection_coeff = genome_mutation->selection_coeff_;
					double rel_fitness = (1.0 + selection_coeff);
					
					w *= ApplyFitnessCallbacks(genome_mutation, -1, rel_fitness, p_fitness_callbacks, genome1, genome2);
					
					if (w <= 0.0)
						return 0.0;
					
					genome_iter++;
				}
			}
			else
			{
				while (genome_iter != genome_max)
				{
					const Mutation *genome_mutation = *genome_iter;
					float selection_coeff = genome_mutation->selection_coeff_;
					
					if (selection_coeff != 0.0f)
					{
						w *= (1.0 + selection_coeff);
						
						if (w <= 0.0)
							return 0.0;
					}
					
					genome_iter++;
				}
			}
		}
		
		return w;
	}
	else
	{
		// both genomes are being modeled, so we need to scan through and figure out which mutations are heterozygous and which are homozygous
		Mutation **genome1_iter = genome1->begin_pointer();
		Mutation **genome2_iter = genome2->begin_pointer();
		
		Mutation **genome1_max = genome1->end_pointer();
		Mutation **genome2_max = genome2->end_pointer();
		
		// first, handle the situation before either genome iterator has reached the end of its genome, for simplicity/speed
		if (genome1_iter != genome1_max && genome2_iter != genome2_max)
		{
			Mutation *genome1_mutation = *genome1_iter, *genome2_mutation = *genome2_iter;
			int genome1_iter_position = genome1_mutation->position_, genome2_iter_position = genome2_mutation->position_;
			
			if (fitness_callbacks_exist)
			{
				do
				{
					if (genome1_iter_position < genome2_iter_position)
					{
						// Process a mutation in genome1 since it is leading
						float selection_coeff = genome1_mutation->selection_coeff_;
						double rel_fitness = (1.0 + genome1_mutation->mutation_type_ptr_->dominance_coeff_ * selection_coeff);
						
						w *= ApplyFitnessCallbacks(genome1_mutation, false, rel_fitness, p_fitness_callbacks, genome1, genome2);
						
						if (w <= 0.0)
							return 0.0;
						
						genome1_iter++;
						
						if (genome1_iter == genome1_max)
							break;
						else {
							genome1_mutation = *genome1_iter;
							genome1_iter_position = genome1_mutation->position_;
						}
					}
					else if (genome1_iter_position > genome2_iter_position)
					{
						// Process a mutation in genome2 since it is leading
						float selection_coeff = genome2_mutation->selection_coeff_;
						double rel_fitness = (1.0 + genome2_mutation->mutation_type_ptr_->dominance_coeff_ * selection_coeff);

						w *= ApplyFitnessCallbacks(genome2_mutation, false, rel_fitness, p_fitness_callbacks, genome1, genome2);
						
						if (w <= 0.0)
							return 0.0;
						
						genome2_iter++;
						
						if (genome2_iter == genome2_max)
							break;
						else {
							genome2_mutation = *genome2_iter;
							genome2_iter_position = genome2_mutation->position_;
						}
					}
					else
					{
						// Look for homozygosity: genome1_iter_position == genome2_iter_position
						int position = genome1_iter_position;
						Mutation **genome1_start = genome1_iter;
						
						// advance through genome1 as long as we remain at the same position, handling one mutation at a time
						do
						{
							float selection_coeff = genome1_mutation->selection_coeff_;
							MutationType *mutation_type_ptr = genome1_mutation->mutation_type_ptr_;
							Mutation **genome2_matchscan = genome2_iter; 
							bool homozygous = false;
							
							// advance through genome2 with genome2_matchscan, looking for a match for the current mutation in genome1, to determine whether we are homozygous or not
							while (genome2_matchscan != genome2_max && (*genome2_matchscan)->position_ == position)
							{
								if (mutation_type_ptr == (*genome2_matchscan)->mutation_type_ptr_ && selection_coeff == (*genome2_matchscan)->selection_coeff_)		// FIXME should use pointer equality!!!
								{
									// a match was found, so we multiply our fitness by the full selection coefficient
									double rel_fitness = (1.0 + selection_coeff);
									
									w *= ApplyFitnessCallbacks(genome1_mutation, true, rel_fitness, p_fitness_callbacks, genome1, genome2);
									homozygous = true;
									
									if (w <= 0.0)
										return 0.0;
									
									break;
								}
								
								genome2_matchscan++;
							}
							
							// no match was found, so we are heterozygous; we multiply our fitness by the selection coefficient and the dominance coefficient
							if (!homozygous)
							{
								double rel_fitness = (1.0 + mutation_type_ptr->dominance_coeff_ * selection_coeff);
								
								w *= ApplyFitnessCallbacks(genome1_mutation, false, rel_fitness, p_fitness_callbacks, genome1, genome2);
								
								if (w <= 0.0)
									return 0.0;
							}
							
							genome1_iter++;
							
							if (genome1_iter == genome1_max)
								break;
							else {
								genome1_mutation = *genome1_iter;
								genome1_iter_position = genome1_mutation->position_;
							}
						} while (genome1_iter_position == position);
						
						// advance through genome2 as long as we remain at the same position, handling one mutation at a time
						do
						{
							float selection_coeff = genome2_mutation->selection_coeff_;
							MutationType *mutation_type_ptr = genome2_mutation->mutation_type_ptr_;
							Mutation **genome1_matchscan = genome1_start; 
							bool homozygous = false;
							
							// advance through genome1 with genome1_matchscan, looking for a match for the current mutation in genome2, to determine whether we are homozygous or not
							while (genome1_matchscan != genome1_max && (*genome1_matchscan)->position_ == position)
							{
								if (mutation_type_ptr == (*genome1_matchscan)->mutation_type_ptr_ && selection_coeff == (*genome1_matchscan)->selection_coeff_)		// FIXME should use pointer equality!!!
								{
									// a match was found; we know this match was already found by the genome1 loop above, so our fitness has already been multiplied appropriately
									homozygous = true;
									break;
								}
								
								genome1_matchscan++;
							}
							
							// no match was found, so we are heterozygous; we multiply our fitness by the selection coefficient and the dominance coefficient
							if (!homozygous)
							{
								double rel_fitness = (1.0 + mutation_type_ptr->dominance_coeff_ * selection_coeff);
								
								w *= ApplyFitnessCallbacks(genome2_mutation, false, rel_fitness, p_fitness_callbacks, genome1, genome2);
								
								if (w <= 0.0)
									return 0.0;
							}
							
							genome2_iter++;
							
							if (genome2_iter == genome2_max)
								break;
							else {
								genome2_mutation = *genome2_iter;
								genome2_iter_position = genome2_mutation->position_;
							}
						} while (genome2_iter_position == position);
						
						// break out if either genome has reached its end
						if (genome1_iter == genome1_max || genome2_iter == genome2_max)
							break;
					}
				} while (true);
			}
			else
			{
				do
				{
					if (genome1_iter_position < genome2_iter_position)
					{
						// Process a mutation in genome1 since it is leading
						float selection_coeff = genome1_mutation->selection_coeff_;
						
						if (selection_coeff != 0.0f)
						{
							w *= (1.0 + genome1_mutation->mutation_type_ptr_->dominance_coeff_ * selection_coeff);
							
							if (w <= 0.0)
								return 0.0;
						}
						
						genome1_iter++;
						
						if (genome1_iter == genome1_max)
							break;
						else {
							genome1_mutation = *genome1_iter;
							genome1_iter_position = genome1_mutation->position_;
						}
					}
					else if (genome1_iter_position > genome2_iter_position)
					{
						// Process a mutation in genome2 since it is leading
						float selection_coeff = genome2_mutation->selection_coeff_;
						
						if (selection_coeff != 0.0f)
						{
							w *= (1.0 + genome2_mutation->mutation_type_ptr_->dominance_coeff_ * selection_coeff);
							
							if (w <= 0.0)
								return 0.0;
						}
						
						genome2_iter++;
						
						if (genome2_iter == genome2_max)
							break;
						else {
							genome2_mutation = *genome2_iter;
							genome2_iter_position = genome2_mutation->position_;
						}
					}
					else
					{
						// Look for homozygosity: genome1_iter_position == genome2_iter_position
						int position = genome1_iter_position;
						Mutation **genome1_start = genome1_iter;
						
						// advance through genome1 as long as we remain at the same position, handling one mutation at a time
						do
						{
							float selection_coeff = genome1_mutation->selection_coeff_;
							
							if (selection_coeff != 0.0f)
							{
								MutationType *mutation_type_ptr = genome1_mutation->mutation_type_ptr_;
								Mutation **genome2_matchscan = genome2_iter; 
								bool homozygous = false;
								
								// advance through genome2 with genome2_matchscan, looking for a match for the current mutation in genome1, to determine whether we are homozygous or not
								while (genome2_matchscan != genome2_max && (*genome2_matchscan)->position_ == position)
								{
									if (mutation_type_ptr == (*genome2_matchscan)->mutation_type_ptr_ && selection_coeff == (*genome2_matchscan)->selection_coeff_) 		// FIXME should use pointer equality!!!
									{
										// a match was found, so we multiply our fitness by the full selection coefficient
										w *= (1.0 + selection_coeff);
										homozygous = true;
										
										if (w <= 0.0)
											return 0.0;
										
										break;
									}
									
									genome2_matchscan++;
								}
								
								// no match was found, so we are heterozygous; we multiply our fitness by the selection coefficient and the dominance coefficient
								if (!homozygous)
								{
									w *= (1.0 + mutation_type_ptr->dominance_coeff_ * selection_coeff);
									
									if (w <= 0.0)
										return 0.0;
								}
							}
							
							genome1_iter++;
							
							if (genome1_iter == genome1_max)
								break;
							else {
								genome1_mutation = *genome1_iter;
								genome1_iter_position = genome1_mutation->position_;
							}
						} while (genome1_iter_position == position);
						
						// advance through genome2 as long as we remain at the same position, handling one mutation at a time
						do
						{
							float selection_coeff = genome2_mutation->selection_coeff_;
							
							if (selection_coeff != 0.0f)
							{
								MutationType *mutation_type_ptr = genome2_mutation->mutation_type_ptr_;
								Mutation **genome1_matchscan = genome1_start; 
								bool homozygous = false;
								
								// advance through genome1 with genome1_matchscan, looking for a match for the current mutation in genome2, to determine whether we are homozygous or not
								while (genome1_matchscan != genome1_max && (*genome1_matchscan)->position_ == position)
								{
									if (mutation_type_ptr == (*genome1_matchscan)->mutation_type_ptr_ && selection_coeff == (*genome1_matchscan)->selection_coeff_)		// FIXME should use pointer equality!!!
									{
										// a match was found; we know this match was already found by the genome1 loop above, so our fitness has already been multiplied appropriately
										homozygous = true;
										break;
									}
									
									genome1_matchscan++;
								}
								
								// no match was found, so we are heterozygous; we multiply our fitness by the selection coefficient and the dominance coefficient
								if (!homozygous)
								{
									w *= (1.0 + mutation_type_ptr->dominance_coeff_ * selection_coeff);
									
									if (w <= 0.0)
										return 0.0;
								}
							}
							
							genome2_iter++;
							
							if (genome2_iter == genome2_max)
								break;
							else {
								genome2_mutation = *genome2_iter;
								genome2_iter_position = genome2_mutation->position_;
							}
						} while (genome2_iter_position == position);
						
						// break out if either genome has reached its end
						if (genome1_iter == genome1_max || genome2_iter == genome2_max)
							break;
					}
				} while (true);
			}
		}
		
		// one or the other genome has now reached its end, so now we just need to handle the remaining mutations in the unfinished genome
		assert(!(genome1_iter != genome1_max && genome2_iter != genome2_max));
		
		// if genome1 is unfinished, finish it
		if (fitness_callbacks_exist)
		{
			while (genome1_iter != genome1_max)
			{
				Mutation *genome1_mutation = *genome1_iter;
				float selection_coeff = genome1_mutation->selection_coeff_;
				double rel_fitness = (1.0 + genome1_mutation->mutation_type_ptr_->dominance_coeff_ * selection_coeff);
				
				w *= ApplyFitnessCallbacks(genome1_mutation, false, rel_fitness, p_fitness_callbacks, genome1, genome2);
				
				if (w <= 0.0)
					return 0.0;
				
				genome1_iter++;
			}
		}
		else
		{
			while (genome1_iter != genome1_max)
			{
				const Mutation *genome1_mutation = *genome1_iter;
				float selection_coeff = genome1_mutation->selection_coeff_;
				
				if (selection_coeff != 0.0f)
				{
					w *= (1.0 + genome1_mutation->mutation_type_ptr_->dominance_coeff_ * selection_coeff);
					
					if (w <= 0.0)
						return 0.0;
				}
				
				genome1_iter++;
			}
		}
		
		// if genome2 is unfinished, finish it
		if (fitness_callbacks_exist)
		{
			while (genome2_iter != genome2_max)
			{
				Mutation *genome2_mutation = *genome2_iter;
				float selection_coeff = genome2_mutation->selection_coeff_;
				double rel_fitness = (1.0 + genome2_mutation->mutation_type_ptr_->dominance_coeff_ * selection_coeff);
				
				w *= ApplyFitnessCallbacks(genome2_mutation, false, rel_fitness, p_fitness_callbacks, genome1, genome2);
				
				if (w <= 0.0)
					return 0.0;
				
				genome2_iter++;
			}
		}
		else
		{
			while (genome2_iter != genome2_max)
			{
				const Mutation *genome2_mutation = *genome2_iter;
				float selection_coeff = genome2_mutation->selection_coeff_;
				
				if (selection_coeff != 0.0f)
				{
					w *= (1.0 + genome2_mutation->mutation_type_ptr_->dominance_coeff_ * selection_coeff);
					
					if (w <= 0.0)
						return 0.0;
				}
				
				genome2_iter++;
			}
		}
		
		return w;
	}
}

void Subpopulation::SwapChildAndParentGenomes(void)
{
	bool will_need_new_children = false;
	
	// If there are any differences between the parent and child genome setups (due to change in subpop size, sex ratio, etc.), we will need to create new child genomes after swapping
	// This is because the parental genomes, which are based on the old parental values, will get swapped in to the children, but they will be out of date.
	if (parent_subpop_size_ != child_subpop_size_ || parent_sex_ratio_ != child_sex_ratio_ || parent_first_male_index_ != child_first_male_index_)
		will_need_new_children = true;
	
	// Execute the genome swap
	child_genomes_.swap(parent_genomes_);
	
	// The parents now have the values that used to belong to the children.
	parent_subpop_size_ = child_subpop_size_;
	parent_sex_ratio_ = child_sex_ratio_;
	parent_first_male_index_ = child_first_male_index_;
	
	// mark the child generation as invalid, until it is generated
	child_generation_valid = false;
	
	// The parental genomes, which have now been swapped into the child genome vactor, no longer fit the bill.  We need to throw them out and generate new genome vectors.
	if (will_need_new_children)
		GenerateChildrenToFit(false);	// false means generate only new children, not new parents
}

//
// Eidos support
//

void Subpopulation::GenerateCachedSymbolTableEntry(void)
{
	// Note that this cache cannot be invalidated, because we are guaranteeing that this object will
	// live for at least as long as the symbol table it may be placed into!
	std::ostringstream subpop_stream;
	
	subpop_stream << "p" << subpopulation_id_;
	
	self_symbol_ = new EidosSymbolTableEntry(subpop_stream.str(), (new EidosValue_Object_singleton_const(this))->SetExternalPermanent());
}

const std::string *Subpopulation::ElementType(void) const
{
	return &gStr_Subpopulation;
}

void Subpopulation::Print(std::ostream &p_ostream) const
{
	p_ostream << *ElementType() << "<p" << subpopulation_id_ << ">";
}

const std::vector<const EidosPropertySignature *> *Subpopulation::Properties(void) const
{
	static std::vector<const EidosPropertySignature *> *properties = nullptr;
	
	if (!properties)
	{
		properties = new std::vector<const EidosPropertySignature *>(*EidosObjectElement::Properties());
		properties->push_back(SignatureForProperty(gID_id));
		properties->push_back(SignatureForProperty(gID_firstMaleIndex));
		properties->push_back(SignatureForProperty(gID_genomes));
		properties->push_back(SignatureForProperty(gID_immigrantSubpopIDs));
		properties->push_back(SignatureForProperty(gID_immigrantSubpopFractions));
		properties->push_back(SignatureForProperty(gID_selfingFraction));
		properties->push_back(SignatureForProperty(gID_sexRatio));
		properties->push_back(SignatureForProperty(gID_individualCount));
		properties->push_back(SignatureForProperty(gID_tag));
		std::sort(properties->begin(), properties->end(), CompareEidosPropertySignatures);
	}
	
	return properties;
}

const EidosPropertySignature *Subpopulation::SignatureForProperty(EidosGlobalStringID p_property_id) const
{
	// Signatures are all preallocated, for speed
	static EidosPropertySignature *idSig = nullptr;
	static EidosPropertySignature *firstMaleIndexSig = nullptr;
	static EidosPropertySignature *genomesSig = nullptr;
	static EidosPropertySignature *immigrantSubpopIDsSig = nullptr;
	static EidosPropertySignature *immigrantSubpopFractionsSig = nullptr;
	static EidosPropertySignature *selfingFractionSig = nullptr;
	static EidosPropertySignature *sexRatioSig = nullptr;
	static EidosPropertySignature *sizeSig = nullptr;
	static EidosPropertySignature *tagSig = nullptr;
	
	if (!idSig)
	{
		idSig =							(EidosPropertySignature *)(new EidosPropertySignature(gStr_id,							gID_id,							true,	kValueMaskInt | kValueMaskSingleton));
		firstMaleIndexSig =				(EidosPropertySignature *)(new EidosPropertySignature(gStr_firstMaleIndex,				gID_firstMaleIndex,				true,	kValueMaskInt | kValueMaskSingleton));
		genomesSig =					(EidosPropertySignature *)(new EidosPropertySignature(gStr_genomes,						gID_genomes,					true,	kValueMaskObject, &gStr_Genome));
		immigrantSubpopIDsSig =			(EidosPropertySignature *)(new EidosPropertySignature(gStr_immigrantSubpopIDs,			gID_immigrantSubpopIDs,			true,	kValueMaskInt));
		immigrantSubpopFractionsSig =	(EidosPropertySignature *)(new EidosPropertySignature(gStr_immigrantSubpopFractions,	gID_immigrantSubpopFractions,	true,	kValueMaskFloat));
		selfingFractionSig =			(EidosPropertySignature *)(new EidosPropertySignature(gStr_selfingFraction,				gID_selfingFraction,			true,	kValueMaskFloat | kValueMaskSingleton));
		sexRatioSig =					(EidosPropertySignature *)(new EidosPropertySignature(gStr_sexRatio,					gID_sexRatio,					true,	kValueMaskFloat | kValueMaskSingleton));
		sizeSig =						(EidosPropertySignature *)(new EidosPropertySignature(gStr_individualCount,				gID_individualCount,			true,	kValueMaskInt | kValueMaskSingleton));
		tagSig =						(EidosPropertySignature *)(new EidosPropertySignature(gStr_tag,							gID_tag,						false,	kValueMaskInt | kValueMaskSingleton));
	}
	
	// All of our strings are in the global registry, so we can require a successful lookup
	switch (p_property_id)
	{
		case gID_id:						return idSig;
		case gID_firstMaleIndex:			return firstMaleIndexSig;
		case gID_genomes:					return genomesSig;
		case gID_immigrantSubpopIDs:		return immigrantSubpopIDsSig;
		case gID_immigrantSubpopFractions:	return immigrantSubpopFractionsSig;
		case gID_selfingFraction:			return selfingFractionSig;
		case gID_sexRatio:					return sexRatioSig;
		case gID_individualCount:			return sizeSig;
		case gID_tag:						return tagSig;
			
			// all others, including gID_none
		default:
			return EidosObjectElement::SignatureForProperty(p_property_id);
	}
}

EidosValue *Subpopulation::GetProperty(EidosGlobalStringID p_property_id)
{
	// All of our strings are in the global registry, so we can require a successful lookup
	switch (p_property_id)
	{
			// constants
		case gID_id:
		{
			// Note that this cache cannot be invalidated, because we are guaranteeing that this object will
			// live for at least as long as the symbol table it may be placed into!
			if (!cached_value_subpop_id_)
				cached_value_subpop_id_ = (new EidosValue_Int_singleton_const(subpopulation_id_))->SetExternalPermanent();
			return cached_value_subpop_id_;
		}
		case gID_firstMaleIndex:
			return new EidosValue_Int_singleton_const(child_generation_valid ? child_first_male_index_ : parent_first_male_index_);
		case gID_genomes:
		{
			EidosValue_Object_vector *vec = new EidosValue_Object_vector();
			
			if (child_generation_valid)
				for (auto genome_iter = child_genomes_.begin(); genome_iter != child_genomes_.end(); genome_iter++)
					vec->PushElement(&(*genome_iter));		// operator * can be overloaded by the iterator
			else
				for (auto genome_iter = parent_genomes_.begin(); genome_iter != parent_genomes_.end(); genome_iter++)
					vec->PushElement(&(*genome_iter));		// operator * can be overloaded by the iterator
			
			return vec;
		}
		case gID_immigrantSubpopIDs:
		{
			EidosValue_Int_vector *vec = new EidosValue_Int_vector();
			
			for (auto migrant_pair = migrant_fractions_.begin(); migrant_pair != migrant_fractions_.end(); ++migrant_pair)
				vec->PushInt(migrant_pair->first);
			
			return vec;
		}
		case gID_immigrantSubpopFractions:
		{
			EidosValue_Float_vector *vec = new EidosValue_Float_vector();
			
			for (auto migrant_pair = migrant_fractions_.begin(); migrant_pair != migrant_fractions_.end(); ++migrant_pair)
				vec->PushFloat(migrant_pair->second);
			
			return vec;
		}
		case gID_selfingFraction:
			return new EidosValue_Float_singleton_const(selfing_fraction_);
		case gID_sexRatio:
			return new EidosValue_Float_singleton_const(child_generation_valid ? child_sex_ratio_ : parent_sex_ratio_);
		case gID_individualCount:
			return new EidosValue_Int_singleton_const(child_generation_valid ? child_subpop_size_ : parent_subpop_size_);
			
			// variables
		case gID_tag:
			return new EidosValue_Int_singleton_const(tag_value_);
			
			// all others, including gID_none
		default:
			return EidosObjectElement::GetProperty(p_property_id);
	}
}

void Subpopulation::SetProperty(EidosGlobalStringID p_property_id, EidosValue *p_value)
{
	switch (p_property_id)
	{
		case gID_tag:
		{
			int64_t value = p_value->IntAtIndex(0);
			
			tag_value_ = value;
			return;
		}
			
		default:
		{
			return EidosObjectElement::SetProperty(p_property_id, p_value);
		}
	}
}

const std::vector<const EidosMethodSignature *> *Subpopulation::Methods(void) const
{
	std::vector<const EidosMethodSignature *> *methods = nullptr;
	
	if (!methods)
	{
		methods = new std::vector<const EidosMethodSignature *>(*EidosObjectElement::Methods());
		methods->push_back(SignatureForMethod(gID_setMigrationRates));
		methods->push_back(SignatureForMethod(gID_setCloningRate));
		methods->push_back(SignatureForMethod(gID_setSelfingRate));
		methods->push_back(SignatureForMethod(gID_setSexRatio));
		methods->push_back(SignatureForMethod(gID_setSubpopulationSize));
		methods->push_back(SignatureForMethod(gID_fitness));
		methods->push_back(SignatureForMethod(gID_outputMSSample));
		methods->push_back(SignatureForMethod(gID_outputSample));
		std::sort(methods->begin(), methods->end(), CompareEidosCallSignatures);
	}
	
	return methods;
}

const EidosMethodSignature *Subpopulation::SignatureForMethod(EidosGlobalStringID p_method_id) const
{
	// Signatures are all preallocated, for speed
	static EidosInstanceMethodSignature *setMigrationRatesSig = nullptr;
	static EidosInstanceMethodSignature *setCloningRateSig = nullptr;
	static EidosInstanceMethodSignature *setSelfingRateSig = nullptr;
	static EidosInstanceMethodSignature *setSexRatioSig = nullptr;
	static EidosInstanceMethodSignature *setSubpopulationSizeSig = nullptr;
	static EidosInstanceMethodSignature *fitnessSig = nullptr;
	static EidosInstanceMethodSignature *outputMSSampleSig = nullptr;
	static EidosInstanceMethodSignature *outputSampleSig = nullptr;
	
	if (!setMigrationRatesSig)
	{
		setMigrationRatesSig = (EidosInstanceMethodSignature *)(new EidosInstanceMethodSignature(gStr_setMigrationRates, kValueMaskNULL))->AddObject("sourceSubpops", &gStr_Subpopulation)->AddNumeric("rates");
		setCloningRateSig = (EidosInstanceMethodSignature *)(new EidosInstanceMethodSignature(gStr_setCloningRate, kValueMaskNULL))->AddNumeric("rate");
		setSelfingRateSig = (EidosInstanceMethodSignature *)(new EidosInstanceMethodSignature(gStr_setSelfingRate, kValueMaskNULL))->AddNumeric_S("rate");
		setSexRatioSig = (EidosInstanceMethodSignature *)(new EidosInstanceMethodSignature(gStr_setSexRatio, kValueMaskNULL))->AddFloat_S("sexRatio");
		setSubpopulationSizeSig = (EidosInstanceMethodSignature *)(new EidosInstanceMethodSignature(gStr_setSubpopulationSize, kValueMaskNULL))->AddInt_S("size");
		fitnessSig = (EidosInstanceMethodSignature *)(new EidosInstanceMethodSignature(gStr_fitness, kValueMaskFloat))->AddInt("indices");
		outputMSSampleSig = (EidosInstanceMethodSignature *)(new EidosInstanceMethodSignature(gStr_outputMSSample, kValueMaskNULL))->AddInt_S("sampleSize")->AddString_OS("requestedSex");
		outputSampleSig = (EidosInstanceMethodSignature *)(new EidosInstanceMethodSignature(gStr_outputSample, kValueMaskNULL))->AddInt_S("sampleSize")->AddString_OS("requestedSex");
	}
	
	// All of our strings are in the global registry, so we can require a successful lookup
	switch (p_method_id)
	{
		case gID_setMigrationRates:		return setMigrationRatesSig;
		case gID_setCloningRate:		return setCloningRateSig;
		case gID_setSelfingRate:		return setSelfingRateSig;
		case gID_setSexRatio:			return setSexRatioSig;
		case gID_setSubpopulationSize:	return setSubpopulationSizeSig;
		case gID_fitness:				return fitnessSig;
		case gID_outputMSSample:		return outputMSSampleSig;
		case gID_outputSample:			return outputSampleSig;
			
			// all others, including gID_none
		default:
			return EidosObjectElement::SignatureForMethod(p_method_id);
	}
}

EidosValue *Subpopulation::ExecuteMethod(EidosGlobalStringID p_method_id, EidosValue *const *const p_arguments, int p_argument_count, EidosInterpreter &p_interpreter)
{
	EidosValue *arg0_value = ((p_argument_count >= 1) ? p_arguments[0] : nullptr);
	EidosValue *arg1_value = ((p_argument_count >= 2) ? p_arguments[1] : nullptr);
	
	
	// All of our strings are in the global registry, so we can require a successful lookup
	switch (p_method_id)
	{
			//
			//	*********************	- (void)setMigrationRates(object sourceSubpops, numeric rates)
			//
#pragma mark -setMigrationRates()
			
		case gID_setMigrationRates:
		{
			int source_subpops_count = arg0_value->Count();
			int rates_count = arg1_value->Count();
			
			if (source_subpops_count != rates_count)
				EIDOS_TERMINATION << "ERROR (Subpopulation::ExecuteMethod): setMigrationRates() requires sourceSubpops and rates to be equal in size." << eidos_terminate();
			
			for (int value_index = 0; value_index < source_subpops_count; ++value_index)
			{
				EidosObjectElement *source_subpop = arg0_value->ObjectElementAtIndex(value_index);
				int source_subpop_id = ((Subpopulation *)(source_subpop))->subpopulation_id_;
				double migrant_fraction = arg1_value->FloatAtIndex(value_index);
				
				population_.SetMigration(subpopulation_id_, source_subpop_id, migrant_fraction);
			}
			
			return gStaticEidosValueNULLInvisible;
		}
			
			
			//
			//	*********************	- (void)setCloningRate(numeric rate)
			//
#pragma mark -setCloningRate()
			
		case gID_setCloningRate:
		{
			int value_count = arg0_value->Count();
			
			if (sex_enabled_)
			{
				// SEX ONLY: either one or two values may be specified; if two, it is female at 0, male at 1
				if ((value_count < 1) || (value_count > 2))
					EIDOS_TERMINATION << "ERROR (Subpopulation::ExecuteMethod): setCloningRate() requires a rate vector containing either one or two values, in sexual simulations." << eidos_terminate();
				
				double female_cloning_fraction = arg0_value->FloatAtIndex(0);
				double male_cloning_fraction = (value_count == 2) ? arg0_value->FloatAtIndex(1) : female_cloning_fraction;
				
				if (female_cloning_fraction < 0.0 || female_cloning_fraction > 1.0)
					EIDOS_TERMINATION << "ERROR (Subpopulation::ExecuteMethod): setCloningRate() requires cloning fractions within [0,1]." << eidos_terminate();
				if (male_cloning_fraction < 0.0 || male_cloning_fraction > 1.0)
					EIDOS_TERMINATION << "ERROR (Subpopulation::ExecuteMethod): setCloningRate() requires cloning fractions within [0,1]." << eidos_terminate();
				
				female_clone_fraction_ = female_cloning_fraction;
				male_clone_fraction_ = male_cloning_fraction;
			}
			else
			{
				// ASEX ONLY: only one value may be specified
				if (value_count != 1)
					EIDOS_TERMINATION << "ERROR (Subpopulation::ExecuteMethod): setCloningRate() requires a rate vector containing exactly one value, in asexual simulations.." << eidos_terminate();
				
				double cloning_fraction = arg0_value->FloatAtIndex(0);
				
				if (cloning_fraction < 0.0 || cloning_fraction > 1.0)
					EIDOS_TERMINATION << "ERROR (Subpopulation::ExecuteMethod): setCloningRate() requires cloning fractions within [0,1]." << eidos_terminate();
				
				female_clone_fraction_ = cloning_fraction;
				male_clone_fraction_ = cloning_fraction;
			}
			
			return gStaticEidosValueNULLInvisible;
		}			
			
			
			//
			//	*********************	- (void)setSelfingRate(numeric$ rate)
			//
#pragma mark -setSelfingRate()
			
		case gID_setSelfingRate:
		{
			double selfing_fraction = arg0_value->FloatAtIndex(0);
			
			if ((selfing_fraction != 0.0) && sex_enabled_)
				EIDOS_TERMINATION << "ERROR (Subpopulation::ExecuteMethod): setSelfingRate() is limited to the hermaphroditic case, and cannot be enabled in sexual simulations." << eidos_terminate();
			
			if (selfing_fraction < 0.0 || selfing_fraction > 1.0)
				EIDOS_TERMINATION << "ERROR (Subpopulation::ExecuteMethod): setSelfingRate() requires a selfing fraction within [0,1]." << eidos_terminate();
			
			selfing_fraction_ = selfing_fraction;
			
			return gStaticEidosValueNULLInvisible;
		}			
			
			
			//
			//	*********************	- (void)setSexRatio(float$ sexRatio)
			//
#pragma mark -setSexRatio()
			
		case gID_setSexRatio:
		{
			// SetSexRatio() can only be called when the child generation has not yet been generated.  It sets the sex ratio on the child generation,
			// and then that sex ratio takes effect when the children are generated from the parents in EvolveSubpopulation().
			if (child_generation_valid)
				EIDOS_TERMINATION << "ERROR (Subpopulation::ExecuteMethod): setSexRatio() called when the child generation was valid" << eidos_terminate();
			
			double sex_ratio = arg0_value->FloatAtIndex(0);
			
			// After we change the subpop sex ratio, we need to generate new children genomes to fit the new requirements
			child_sex_ratio_ = sex_ratio;
			GenerateChildrenToFit(false);	// false means generate only new children, not new parents
			
			return gStaticEidosValueNULLInvisible;
		}
			
			
			//
			//	*********************	- (void)setSubpopulationSize(integer$ size)
			//
#pragma mark -setSubpopulationSize()
			
		case gID_setSubpopulationSize:
		{
			int subpop_size = (int)arg0_value->IntAtIndex(0);
			
			population_.SetSize(subpopulation_id_, subpop_size);
			
			return gStaticEidosValueNULLInvisible;
		}
			
			
			//
			//	*********************	- (float)fitness(integer indices)
			//
#pragma mark -fitness()
			
		case gID_fitness:
		{
			if (child_generation_valid)
				EIDOS_TERMINATION << "ERROR (Subpopulation::ExecuteMethod): fitness() may only be called when the parental generation is active (before or during offspring generation)." << eidos_terminate();
			if (cached_fitness_size_ == 0)
				EIDOS_TERMINATION << "ERROR (Subpopulation::ExecuteMethod): fitness() may not be called while fitness values are being calculated, or before the first time they are calculated." << eidos_terminate();
			
			bool do_all_indices = (arg0_value->Type() == EidosValueType::kValueNULL);
			int index_count = (do_all_indices ? parent_subpop_size_ : arg0_value->Count());
			
			if (index_count == 1)
			{
				int index = (do_all_indices ? 0 : (int)arg0_value->IntAtIndex(0));
				double fitness = cached_parental_fitness_[index];
				
				return new EidosValue_Float_singleton_const(fitness);
			}
			else
			{
				EidosValue_Float_vector *float_return = new EidosValue_Float_vector();
				
				for (int value_index = 0; value_index < index_count; value_index++)
				{
					int index = (do_all_indices ? value_index : (int)arg0_value->IntAtIndex(value_index));
					double fitness = cached_parental_fitness_[index];
					
					float_return->PushFloat(fitness);
				}
				
				return float_return;
			}
		}
			
			
			//
			//	*********************	- (void)outputMSSample(integer$ sampleSize, [string$ requestedSex])
			//	*********************	- (void)outputSample(integer$ sampleSize, [string$ requestedSex])
			//
#pragma mark -outputMSSample()
#pragma mark -outputSample()
			
		case gID_outputMSSample:
		case gID_outputSample:
		{
			int sample_size = (int)arg0_value->IntAtIndex(0);
			IndividualSex requested_sex = IndividualSex::kUnspecified;
			
			if (p_argument_count == 2)
			{
				string sex_string = arg1_value->StringAtIndex(0);
				
				if (sex_string.compare("M") == 0)
					requested_sex = IndividualSex::kMale;
				else if (sex_string.compare("F") == 0)
					requested_sex = IndividualSex::kFemale;
				else if (sex_string.compare("*") == 0)
					requested_sex = IndividualSex::kUnspecified;
			}
			
			SLiMSim &sim = population_.sim_;
			
			EIDOS_OUTSTREAM << "#OUT: " << sim.Generation() << " R p" << subpopulation_id_ << " " << sample_size;
			
			if (sim.SexEnabled())
				EIDOS_OUTSTREAM << " " << requested_sex;
			
			EIDOS_OUTSTREAM << endl;
			
			if (p_method_id == gID_outputSample)
				population_.PrintSample(subpopulation_id_, sample_size, requested_sex);
			else
				population_.PrintSample_ms(subpopulation_id_, sample_size, sim.Chromosome(), requested_sex);
			
			return gStaticEidosValueNULLInvisible;
		}
			
			// all others, including gID_none
		default:
			return EidosObjectElement::ExecuteMethod(p_method_id, p_arguments, p_argument_count, p_interpreter);
	}
}




































































