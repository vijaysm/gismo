/** @file gsHFitting.h

    @brief Adaptive fitting using hierarchical splines

    This file is part of the G+Smo library.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.

    Author(s): C. Giannelli, G. Kiss
*/

#pragma once

#include <gsModeling/gsFitting.h>
#include <gsHSplines/gsHTensorBasis.h>

namespace gismo {

/**
    \brief
    This class applies hierarchical fitting of parametrized point clouds.

    \tparam T coefficient type

    \ingroup HSplines
*/

template <unsigned d, class T>
class gsHFitting : public gsFitting<T>
{
public:

    typedef typename gsBSplineTraits<d,T>::Basis tensorBasis;

public:
    /// Default constructor
    gsHFitting();

    /**
        \brief
        Main constructor of the fitting class

        \param param_values a matrix containing the parameter values
        that parametrize the \a points

        \param points The points to be fitted

        \param basis  Hiearchical basis to use for fitting

        \param refin Percentage of errors to refine (if this strategy is chosen)

        \param extension Extension to apply to marked cells

        \param lambda Smoothing parameter
    */
    gsHFitting(gsMatrix<T> const & param_values,
               gsMatrix<T> const & points,
               gsHTensorBasis<d,T> & basis,
               T refin, const std::vector<unsigned> & extension,
               T lambda = 0)
    : gsFitting<T>(param_values, points, basis)
    {
        GISMO_ASSERT((refin >=0) && (refin <=1),
                     "Refinement percentage must be between 0 and 1." );
        GISMO_ASSERT(extension.size() == d, "Extension is not of the right dimension");
        GISMO_ASSERT( (gsAsConstVector<unsigned>(extension).array()>=0).all(),
                      "Extension must be a positive number.");

        m_ref    = refin;     //how many % to refine

        m_ext    = extension;

        m_lambda = lambda;    // Smoothing parameter

        m_max_error = m_min_error = 0;

        m_pointErrors.reserve(m_param_values.cols());
    }

public:

    /**
     * @brief iterative_refine iteratively refine the basis
     *
     *
     * @param iterations maximum number of iterations
     *
     * @param tolerance (>=0) if the max error is below the tolerance the refinement stops
     *
     * @param err_threshold if non negative all cells with errors
     * bigger than the threshold are refined /
     * If it is equal to -1 the m_ref percentage is used
     * 0 = global refinement
     */
    void iterativeRefine(int iterations, T tolerance, T err_threshold = -1);

    /**
     * @brief nextIteration One step of the refinement of iterative_refine(...);
     * @param tolerance (>=0) if the maximum error is below the tolerance the refinement stops;
     * @param err_threshold the same as in iterative_refine(...).
     */
    bool nextIteration(T tolerance, T err_threshold);

    /// Return the refinement percentage
    T getRefPercentage() const
    {
        return m_ref;
    }

    /// Returns the chosen cell extension
    const std::vector<unsigned> & get_extension() const
    {
        return m_ext;
    }

    /// Sets the refinement percentage
    void setRefPercentage(double refPercent)
    {
        GISMO_ASSERT((refPercent >=0) && (refPercent <=1), "Invalid percentage" );
        m_ref = refPercent;
    }

    /// Sets the cell extension
    void setExtension(std::vector<unsigned> const & extension)
    {
        GISMO_ASSERT( (gsAsConstVector<unsigned>(extension).array()>=0).all(),
                      "Extension must be a positive number.");
        GISMO_ASSERT(extension.size()== static_cast<size_t>(this->m_basis.dim()),
                     "Error in dimension");
        m_ext = extension;
    }

    /// Returns boxes which define refinment area.
    std::vector<unsigned> getBoxes(const std::vector<T>& errors,
                                   const T threshold);

protected:
    /// Appends a box around parameter to the boxes only if the box is not
    /// already in boxes
    virtual void appendBox(std::vector<unsigned>& boxes,
                   std::vector<unsigned>& cells,
                   const gsVector<T>& parameter);

    /// Identifies the threshold from where we should refine
    T setRefineThreshold(const std::vector<T>& errors);

    /// Checks if a_cell is already inserted in container of cells
    static bool isCellAlreadyInserted(const gsVector<unsigned, d>& a_cell,
                                      const std::vector<unsigned>& cells);

    /// Appends a box to the end of boxes (This function also works for cells)
    static void append(std::vector<unsigned>& boxes,
                       const gsVector<unsigned>& box)
    {
        for (index_t col = 0; col != box.rows(); col++)
            boxes.push_back(box[col]);
    }

protected:

    /// How many % to refine - 0-1 interval
    T m_ref;

    /// Smoothing parameter
    T m_lambda;

    /// Size of the extension
    std::vector<unsigned> m_ext;

    using gsFitting<T>::m_param_values;
    using gsFitting<T>::m_points;
    using gsFitting<T>::m_basis;
    using gsFitting<T>::m_result;

    using gsFitting<T>::m_pointErrors;
    using gsFitting<T>::m_max_error;
    using gsFitting<T>::m_min_error;
};



template<unsigned d, class T>
bool gsHFitting<d, T>::nextIteration(T tolerance, T err_threshold)
{
    // INVARIANT
    // look at iterativeRefine

    if ( m_pointErrors.size() != 0 )
    {

        if ( m_max_error > tolerance )
        {
            // if err_treshold is -1 we refine the m_ref percent of the whole domain
            T threshold = (err_threshold >= 0) ? err_threshold : setRefineThreshold(m_pointErrors);

            std::vector<unsigned> boxes = getBoxes(m_pointErrors, threshold);
            if(boxes.size()==0)
                return false;

            gsHTensorBasis<d, T>* basis = static_cast<gsHTensorBasis<d,T> *> (this->m_basis);
            basis->refineElements(boxes);

            gsDebug << "inserted " << boxes.size() / (2 * d + 1) << " boxes.\n";
        }
        else
        {
            gsDebug << "Tolerance reached.\n";
            return false;
        }
    }

    // We run one fitting step and compute the errors
    this->compute(m_lambda);
    this->computeErrors();

    return true;
}

template<unsigned d, class T>
void gsHFitting<d, T>::iterativeRefine(int numIterations, T tolerance, T err_threshold)
{
    // INVARIANT:
    // m_pointErrors contains the point-wise errors of the fitting
    // therefore: if the size of m_pointErrors is 0, there was no fitting up to this point

    if ( m_pointErrors.size() == 0 )
    {
        this->compute(m_lambda);
        this->computeErrors();
    }

    bool newIteration;
    for( int i = 0; i < numIterations; i++ )
    {
        newIteration = nextIteration( tolerance, err_threshold );
        if( m_max_error <= tolerance )
        {
            gsDebug << "Tolerance reached at iteration: " << i << "\n";
            break;
        }
        if( !newIteration )
        {
            gsDebug << "No more Boxes to insert at iteration: " << i << "\n";
            break;
        }
    }
}

template <unsigned d, class T>
std::vector<unsigned> gsHFitting<d, T>::getBoxes(const std::vector<T>& errors,
                                                 const T threshold)
{
    // cells contains lower corners of elements marked for refinment from maxLevel
    std::vector<unsigned> cells;

    // boxes contains elements marked for refinement from differnet levels,
    // format: { level lower-corners  upper-corners ... }
    std::vector<unsigned> boxes;

    for (std::size_t index = 0; index != errors.size(); index++)
    {
        if (threshold <= errors[index])
        {
            appendBox(boxes, cells, this->m_param_values.col(index));
        }
    }

    return boxes;
}

template <unsigned d, class T>
void gsHFitting<d, T>::appendBox(std::vector<unsigned>& boxes,
                                  std::vector<unsigned>& cells,
                                  const gsVector<T>& parameter)
{
    gsTHBSplineBasis<d, T>* basis = static_cast< gsTHBSplineBasis<d,T>* > (this->m_basis);
    const int maxLvl = basis->maxLevel();
    const tensorBasis & tBasis = *(basis->getBases()[maxLvl]);

    // get a cell
    gsVector<unsigned, d> a_cell;

    for (unsigned dim = 0; dim != d; dim++)
    {
        const gsKnotVector<T> & kv = tBasis.component(dim).knots();
        a_cell(dim) = static_cast<unsigned>(kv.uFind(parameter(dim)).uIndex());
    }

    if (!isCellAlreadyInserted(a_cell, cells))
    {
        append(cells, a_cell);

        // get level of a cell
        gsVector<unsigned, d> a_cell_upp = a_cell + gsVector<unsigned, d>::Ones();
        const int cell_lvl = basis->tree().query3(a_cell, a_cell_upp, maxLvl) + 1;

        // get the box
        gsVector<unsigned> box(2 * d + 1);
        box[0] = cell_lvl;
        for (unsigned dim = 0; dim != d; dim++)
        {
            const unsigned numBreaks = basis->numBreaks(cell_lvl, dim) - 1 ;

            unsigned lowIndex = 0;
            if (cell_lvl < maxLvl)
            {
                const unsigned shift = maxLvl - cell_lvl;
                lowIndex = (a_cell(dim) >> shift);
            }
            else
            {
                const unsigned shift = cell_lvl - maxLvl;
                lowIndex = (a_cell(dim) << shift);
            }

            // apply extensions
            unsigned low = ( (lowIndex > m_ext[dim]) ? (lowIndex - m_ext[dim]) : 0 );
            unsigned upp = ( (lowIndex + m_ext[dim] + 1 < numBreaks) ?
                             (lowIndex + m_ext[dim] + 1) : numBreaks );

            box[1 + dim    ] = low;
            box[1 + d + dim] = upp;
        }

        append(boxes, box);
    }
}


template <unsigned d, class T>
bool gsHFitting<d, T>::isCellAlreadyInserted(const gsVector<unsigned, d>& a_cell,
                                             const std::vector<unsigned>& cells)
{

    for (std::size_t i = 0; i != cells.size(); i += a_cell.rows())
    {
        int commonEntries = 0;
        for (index_t col = 0; col != a_cell.rows(); col++)
        {
            if (cells[i + col] == a_cell[col])
            {
                commonEntries++;
            }
        }

        if (commonEntries == a_cell.rows())
        {
            return true;
        }
    }

    return false;
}

template<unsigned d, class T>
T gsHFitting<d, T>::setRefineThreshold(const std::vector<T>& errors )
{
    std::vector<T> errorsCopy = errors;
    const std::size_t i = cast<T,std::size_t>(errorsCopy.size() * (1.0 - m_ref));
    typename std::vector<T>::iterator pos = errorsCopy.begin() + i;
    std::nth_element(errorsCopy.begin(), pos, errorsCopy.end());
    return *pos;
}


}// namespace gismo
