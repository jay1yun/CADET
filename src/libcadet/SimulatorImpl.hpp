// =============================================================================
//  CADET - The Chromatography Analysis and Design Toolkit
//  
//  Copyright © 2008-2016: The CADET Authors
//            Please see the AUTHORS and CONTRIBUTORS file.
//  
//  All rights reserved. This program and the accompanying materials
//  are made available under the terms of the GNU Public License v3.0 (or, at
//  your option, any later version) which accompanies this distribution, and
//  is available at http://www.gnu.org/licenses/gpl.html
// =============================================================================

/**
 * @file 
 * Simulator implementation.
 */

#ifndef LIBCADET_SIMULATOR_IMPL_HPP_
#define LIBCADET_SIMULATOR_IMPL_HPP_

#include <vector>
#include <unordered_map>

#include "SundialsVector.hpp"
#include <idas/idas_impl.h>

#include "cadet/Simulator.hpp"
#include "AutoDiff.hpp"
#include "SlicedVector.hpp"
#include "common/Timer.hpp"

namespace cadet
{

int residualDaeWrapper(double t, N_Vector y, N_Vector yDot, N_Vector res, void* userData);

int linearSolveWrapper(IDAMem IDA_mem, N_Vector rhs, N_Vector weight, N_Vector yCur, N_Vector yDotCur, N_Vector resCur);

int residualSensWrapper(int ns, double t, N_Vector y, N_Vector yDot, N_Vector res, 
		N_Vector* yS, N_Vector* ySDot, N_Vector* resS,
		void *userData, N_Vector tmp1, N_Vector tmp2, N_Vector tmp3);

namespace model
{
	class ModelSystem;
}

/**
 * @brief Provides functionality to simulate a model using a time integrator
 * @details This class is responsible for managing the time integration process 
 *          and holds all memory associated with that (e.g., state vectors).
 */
class Simulator : public ISimulator
{
public:

	Simulator();

	virtual ~Simulator() CADET_NOEXCEPT;

	virtual std::unordered_map<ParameterId, double> getAllParameterValues() const;
	virtual bool hasParameter(const ParameterId& pId) const;
	virtual void setSensitiveParameter(const ParameterId& id);
	virtual void setSensitiveParameter(const ParameterId& id, double absTolS);
	virtual void clearSensParams();
	virtual unsigned int numSensParams() const CADET_NOEXCEPT;
	virtual void setSensitiveParameterValue(const ParameterId& id, double value);
	virtual void setSensitiveParameterValue(unsigned int idx, double value);
	virtual void setSensitiveParameterFactors(unsigned int idx, double const* factors);
	virtual void setSensitiveParameter(ParameterId const* ids, double const* diffFactors, unsigned int numParams, double absTolS);
	virtual void setSensitiveParameter(ParameterId const* ids, unsigned int numParams, double absTolS);
	virtual void setParameterValue(const ParameterId& id, double value);

	virtual void setSolutionTimes(const std::vector<double>& solutionTimes);
	virtual const std::vector<double>& getSolutionTimes() const;
	virtual void setSectionTimes(const std::vector<double>& sectionTimes);
	virtual void setSectionTimes(const std::vector<double>& sectionTimes, const std::vector<bool>& sectionContinuity);

	virtual void initializeModel(IModelSystem& model);

	virtual void setInitialCondition(IParameterProvider& paramProvider);
	virtual void setInitialCondition(double const* const initState);
	virtual void setInitialCondition(double const* const initState, double const* const initStateDot);
	virtual void setInitialConditionFwdSensitivities(double const * const* const initSens, double const * const* const initSensDot);
	virtual void skipConsistentInitialization();
	virtual void setConsistentInitialization(ConsistentInitialization ci);
	virtual void setConsistentInitializationSens(ConsistentInitialization ci);

	virtual void initializeFwdSensitivities();
	virtual void initializeFwdSensitivities(double const * const* const initSens, double const * const* const initSensDot);

	virtual void setSolutionRecorder(ISolutionRecorder* recorder);

	virtual void integrate();

	virtual double const* getLastSolution(unsigned int& len) const;
	virtual double const* getLastSolutionDerivative(unsigned int& len) const;

	virtual const std::vector<double const*> getLastSensitivities(unsigned int& len) const;
	virtual const std::vector<double const*> getLastSensitivityDerivatives(unsigned int& len) const;

	virtual void configure(IParameterProvider& paramProvider);
	virtual void reconfigure(IParameterProvider& paramProvider);
	virtual void configureTimeIntegrator(double relTol, double absTol, double initStepSize, unsigned int maxSteps);
	virtual void configureTimeIntegrator(double relTol, double absTol, const std::vector<double>& initStepSizes, unsigned int maxSteps);
	virtual void setSensitivityErrorTolerance(double relTol, double const* absTol);

	virtual void setRelativeErrorTolerance(double relTol);
	virtual void setAbsoluteErrorTolerance(double absTol);
	virtual void setAbsoluteErrorTolerance(const std::vector<double>& absTol);
	virtual void setAlgebraicErrorTolerance(double algTol) CADET_NOEXCEPT { _algTol = algTol; }
	virtual void setInitialStepSize(double stepSize);
	virtual void setInitialStepSize(const std::vector<double>& stepSize);
	virtual void setMaximumSteps(unsigned int maxSteps);
	virtual void setRelativeErrorToleranceSens(double relTol);

	virtual bool reconfigureModel(IParameterProvider& paramProvider);
	virtual bool reconfigureModel(IParameterProvider& paramProvider, unsigned int unitOpIdx);

	virtual IModelSystem* const model() CADET_NOEXCEPT;
	virtual IModelSystem const* const model() const CADET_NOEXCEPT;

	virtual unsigned int numDofs() const CADET_NOEXCEPT;
	virtual void setNumThreads(unsigned int nThreads) const;

	virtual double lastSimulationDuration() const CADET_NOEXCEPT { return _lastIntTime; }
	virtual double totalSimulationDuration() const CADET_NOEXCEPT { return _timerIntegration.totalElapsedTime(); }
protected:

	/**
	 * @brief Clears memory used for time integration of a model
	 */
	void clearModel() CADET_NOEXCEPT;

	/**
	 * @brief Writes the solution at time point t
	 * @param [in] t Current time point
	 */
	void writeSolution(double t);

	/**
	 * @brief Computes the index of the next section from the given time @p t
	 * @details Returns the lowest index @c i with @f$ t_i \geq t @f$, where 
	 *          @f$ t_i @f$ is an element of @c _sectionTimes.
	 * @param [in] t Current time
	 * @param [in] startIdx Index of the first section the search should begin with
	 * @return Index of the next section corresponding to time @p t
	 */
	unsigned int getNextSection(double t, unsigned int startIdx) const;

	/**
	 * @brief Computes the index of the current section from the given time @p t
	 * @details Returns the lowest index @c i with @f$ t_i \leq t \leq t_{i+1} @f$,
	 *          where @f$ t_i @f$ is an element of @c _sectionTimes.
	 *          Whereas getNextSection() returns the index of the next section,
	 *          this function stays within the current section due to the second
	 *          inequality condition.
	 * @param [in] t Current time
	 * @return Index of the current section corresponding to time @p t
	 */
	unsigned int getCurrentSection(double t) const;

	/**
	 * @brief Enables or disables sensitivities in IDAS and allocates space for sensitivity state vectors
	 * @param [in] nSens Number of sensitivities
	 */
	void preFwdSensInit(unsigned int nSens);

	/**
	 * @brief Sets up IDAS for computing sensitivities
	 * @details Main objective is handing the initial values of the sensitivity subsystems over to IDAS
	 * 
	 * @param [in] nSens Number of sensitivities
	 */
	void postFwdSensInit(unsigned int nSens);

	/**
	 * @brief Returns the number of AD directions that are assigned to a parameter sensitivity
	 * @return Total number of AD directions used for parameter sensitivities
	 */
	inline unsigned int numSensitivityAdDirections() const { return _sensitiveParams.slices(); }

	/**
	 * @brief Sets the SECTION_TIMES parameter sensitive that matches the given parameter @p id
	 * @param [in] id Parameter Id of the sensitive parameter
	 * @param [in] adDirection AD direction assigned to this parameter
	 * @param [in,out] adValue On entry value of the derivative in the given direction, on exit the (possibly corrected) value
	 * @return @c true if the given parameter matches a SECTION_TIMES parameter, otherwise @c false
	 */
	bool setSectionTimesSensitive(const ParameterId& id, unsigned int adDirection, double adValue);

	/**
	 * @brief Sets all AD directions for sensitive parameters again
	 * @details This is necessary if the underlying active datatypes have been changed.
	 */
	void resetSensParams();

	/**
	 * @brief Transforms the user solution times according to the (transformed) section times 
	 */
	void transformSolutionTimes();

	/**
	 * @brief Calculates _transformedTimes from _sectionTimes via time transformation
	 * @details Since the _sectionTimes are reset, it may be necessary to reset their AD directions.
	 *          This is done if @p resetSens is set to @c true.
	 * @param [in] resetSens Set to @c true if AD directions should be updated, otherwise @c false
	 */
	void calculateTimeTransformation(bool resetSens);

	/**
	 * @brief Updates the error tolerances in IDAS
	 * @details Sets the absolute and relative error tolerances in IDAS. If the absolute error
	 *          tolerances are given as vector / array, a model is required to expand the
	 *          error specification or fill in missing DOFs. If a model is not present, nothing
	 *          happens (i.e., the errors are not updated within IDAS). If IDAS has not been
	 *          initialized yet, nothing happens.
	 */
	void updateMainErrorTolerances();

	const active timeFactor(unsigned int curSec) const;
	inline const active timeFactor() const { return timeFactor(_curSec); }

	template <typename time_t>
	double toTransformedTime(double t, const std::vector<time_t>& oldTimes, const std::vector<double>& newTimes) const;

	inline double toTransformedTime(double t) const { return toTransformedTime(t, _sectionTimes, _transformedTimes); }
	inline active toRealTime(double t) const { return toRealTime(t, _curSec); }
	active toRealTime(double t, unsigned int curSec) const;

	friend int ::cadet::residualDaeWrapper(double t, N_Vector y, N_Vector yDot, N_Vector res, void* userData);

	friend int ::cadet::linearSolveWrapper(IDAMem IDA_mem, N_Vector rhs, N_Vector weight, N_Vector yCur, N_Vector yDotCur, N_Vector resCur);

	friend int ::cadet::residualSensWrapper(int ns, double t, N_Vector y, N_Vector yDot, N_Vector res, 
			N_Vector* yS, N_Vector* ySDot, N_Vector* resS,
			void *userData, N_Vector tmp1, N_Vector tmp2, N_Vector tmp3);

	model::ModelSystem* _model; //!< Simulated model, not owned by the Simulator

	ISolutionRecorder* _solRecorder;

	void* _idaMemBlock; //!< IDAS internal memory

	std::vector<double> _transformedTimes; //!< Stores the section time points (start, end)
	
	/**
	 * @brief Determines whether the transition from section i to section i+1 is continuous.
	 * @details The solver will be reset only at discontinuous transitions. The i-th element 
	 *          corresponds to the transition from _sectionTimes[i+1] to _sectionTimes[i+2]. 
	 *          Therefore size = nsec - 1.
	 */
	std::vector<bool> _sectionContinuity;

	std::vector<double> _solutionTimes; //!< Contains the time transformed user specified times for writing solutions to the output
	std::vector<double> _solutionTimesOriginal; //!< Contains the original user specified times for writing solutions to the output

	N_Vector _vecStateY; //!< IDAS state vector	
	N_Vector _vecStateYdot; //!< IDAS state vector time derivative
	N_Vector* _vecFwdYs; //!< IDAS sensitivities vector	
	N_Vector* _vecFwdYsDot; //!< IDAS sensitivities vector time derivative
	util::SlicedVector<ParameterId> _sensitiveParams; //!< Stores (fused) sensitive parameters
	std::vector<double> _sensitiveParamsFactor; //!< Stores the factors of the linear sensitive parameter combinations
	std::vector<active> _sectionTimes; //!< Stores the AD variables used for SECTION_TIMES parameter derivatives
	
	double _relTolS; //!< Relative tolerance for forward sensitivity systems in the time integration
	std::vector<double> _absTolS; //!< Absolute tolerances for forward sensitivity systems in the time integration

	std::vector<double> _absTol; //!< Absolute tolerance for the original system in the time integration
	double _relTol; //!< Relative tolerance for the original system in the time integration
	double _algTol; //!< Tolerance for the solution of algebraic equations in the consistent initialization
	std::vector<double> _initStepSize; //!< Initial step size for the time integrator
	unsigned int _maxSteps; //!< Maximum number of time integration steps

	SectionIdx _curSec; //!< Index of the current section

	bool _skipConsistencyStateY; //!< Flag that determines whether the consistent initialization is skipped
	bool _skipConsistencySensitivity; //!< Flag that determines whether the consistent initialization of the sensitivity systems is skipped

	ConsistentInitialization _consistentInitMode; //!< Mode that determines consistent initialization behavior
	ConsistentInitialization _consistentInitModeSens; //!< Mode that determines consistent initialization behavior of the sensitivity systems

	active* _vecADres; //!< Vector of AD datatypes for holding the residual
	active* _vecADy; //!< Vector of AD datatypes for holding the state vector

	Timer _timerIntegration; //!< Timer measuring the duration of the call to integrate()
	double _lastIntTime; //!< Last simulation duration
};

} // namespace cadet

#endif  // LIBCADET_SIMULATOR_IMPL_HPP_
