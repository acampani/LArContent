/**
 *  @file   LArContent/src/LArClusterAssociation/TransverseAssociationAlgorithm.cc
 * 
 *  @brief  Implementation of the cluster association algorithm class.
 * 
 *  $Log: $
 */

#include "Pandora/AlgorithmHeaders.h"

#include "LArClusterAssociation/TransverseAssociationAlgorithm.h"

#include "LArHelpers/LArClusterHelper.h"

using namespace pandora;

namespace lar
{

void TransverseAssociationAlgorithm::GetListOfCleanClusters(const ClusterList *const pClusterList, ClusterVector &clusterVector) const
{
    clusterVector.clear();
    clusterVector.insert(clusterVector.begin(), pClusterList->begin(), pClusterList->end());
    std::sort(clusterVector.begin(), clusterVector.end(), LArClusterHelper::SortByNOccupiedLayers);
}

//------------------------------------------------------------------------------------------------------------------------------------------

void TransverseAssociationAlgorithm::PopulateClusterAssociationMap(const ClusterVector &inputClusters, ClusterAssociationMap &clusterAssociationMap) const
{
    TransverseClusterList transverseClusterList;

    try
    {
        ClusterVector transverseClusters, longitudinalClusters;
        this->SeparateInputClusters(inputClusters, transverseClusters, longitudinalClusters);
        this->FillTransverseClusterList(transverseClusters, longitudinalClusters, transverseClusterList);

        LArClusterMergeMap forwardMergeMap, backwardMergeMap;
        this->FillClusterMergeMaps(transverseClusterList, forwardMergeMap, backwardMergeMap);
        this->FillClusterAssociationMap(forwardMergeMap, backwardMergeMap, clusterAssociationMap);
    }
    catch (StatusCodeException &statusCodeException)
    {
        std::cout << "TransverseAssociationAlgorithm: exception " << statusCodeException.ToString() << std::endl;
    }

    for (TransverseClusterList::const_iterator iter = transverseClusterList.begin(), iterEnd = transverseClusterList.end(); iter != iterEnd; ++iter)
    {
        delete *iter;
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

void TransverseAssociationAlgorithm::SeparateInputClusters(const ClusterVector &inputVector, ClusterVector &transverseVector, ClusterVector &longitudinalVector) const
{
    for (ClusterVector::const_iterator iter = inputVector.begin(), iterEnd = inputVector.end(); iter != iterEnd; ++iter)
    {
        Cluster *pCluster = *iter;

        // All short clusters are building blocks for transverse clusters
        if (pCluster->GetNCaloHits() <= m_transverseClusterMaxCaloHits)
	{
	    transverseVector.push_back(pCluster);
	}

        // Separate long clusters into transverse and longitudinal clusters
        else 
	{
            static const CartesianVector longitudinalDirection(0.f, 0.f, 1.f);
            const ClusterHelper::ClusterFitResult &clusterFitResult(pCluster->GetFitToAllHitsResult());

            if (clusterFitResult.IsFitSuccessful())
            {
                const CartesianVector &fitDirection(clusterFitResult.GetDirection());
                const float clusterLengthSquared(LArClusterHelper::GetLengthSquared(pCluster));

                if (std::fabs(fitDirection.GetDotProduct(longitudinalDirection)) < m_clusterCosAngle)
		{
                    if (clusterLengthSquared < m_transverseClusterMaxLength * m_transverseClusterMaxLength)
                        transverseVector.push_back(pCluster);
		}
                else
		{
		    if (clusterLengthSquared > m_longitudinalClusterMinLength * m_longitudinalClusterMinLength)
                         longitudinalVector.push_back(pCluster);
		}
	    }
	}
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

void TransverseAssociationAlgorithm::FillTransverseClusterList(const ClusterVector &transverseVector, const ClusterVector &longitudinalVector,
    TransverseClusterList &transverseClusterList) const
{
    for (ClusterVector::const_iterator iter = transverseVector.begin(), iterEnd = transverseVector.end(); iter != iterEnd; ++iter)
    {
        Cluster *pCluster = *iter;
        ClusterVector associatedClusters;

        this->GetAssociatedClusters(pCluster, transverseVector, longitudinalVector, associatedClusters);

        if (this->GetTransverseLength(pCluster,associatedClusters) < m_minTransverseDisplacement)
	    continue;

        transverseClusterList.push_back(new LArTransverseCluster(pCluster, associatedClusters));
    }

// ---- BEGIN DISPLAY ----
ClusterList tempList1, tempList2, tempList3;
tempList1.insert(transverseVector.begin(), transverseVector.end());
tempList2.insert(longitudinalVector.begin(), longitudinalVector.end());

for ( unsigned int nCluster = 0; nCluster < transverseClusterList.size(); ++nCluster )
{
LArTransverseCluster* transverseCluster = transverseClusterList.at(nCluster);
tempList3.insert(transverseCluster->GetSeedCluster());
tempList1.erase(transverseCluster->GetSeedCluster());
}

PandoraMonitoringApi::SetEveDisplayParameters(0, 0, -1.f, 1.f); 
PandoraMonitoringApi::VisualizeClusters(&tempList1, "RemnantClusters", GREEN);
PandoraMonitoringApi::VisualizeClusters(&tempList2, "LongitudinalClusters", BLUE);
PandoraMonitoringApi::VisualizeClusters(&tempList3, "TransverseClusters", RED);
PandoraMonitoringApi::ViewEvent();
// ---- END DISPLAY ----
}

//------------------------------------------------------------------------------------------------------------------------------------------

void TransverseAssociationAlgorithm::FillClusterMergeMaps(const TransverseClusterList &transverseClusterList, LArClusterMergeMap &forwardMergeMap,
    LArClusterMergeMap &backwardMergeMap) const
{
    // Construct an initial set of forward/backward associations
    LArClusterMergeMap fullForwardMergeMap, fullBackwardMergeMap;

    for (TransverseClusterList::const_iterator iter1 = transverseClusterList.begin(), iterEnd1 = transverseClusterList.end(); iter1 != iterEnd1; ++iter1)
    {
        LArTransverseCluster *pInnerTransverseCluster = *iter1;
        Cluster *pInnerCluster(pInnerTransverseCluster->GetSeedCluster());

        for (TransverseClusterList::const_iterator iter2 = transverseClusterList.begin(), iterEnd2 = transverseClusterList.end(); iter2 != iterEnd2; ++iter2)
        {
            LArTransverseCluster *pOuterTransverseCluster = *iter2;
            Cluster *pOuterCluster(pOuterTransverseCluster->GetSeedCluster());

            if (pInnerCluster == pOuterCluster)
                continue;

            if (this->IsExtremalCluster(true, pInnerCluster, pOuterCluster) && this->IsExtremalCluster(false, pOuterCluster, pInnerCluster))
            {
                if (this->IsTransverseAssociated(pInnerTransverseCluster, pOuterTransverseCluster))
                {
                    fullForwardMergeMap[pInnerCluster].insert(pOuterCluster);
                    fullBackwardMergeMap[pOuterCluster].insert(pInnerCluster);
                }
            }
        }
    } 

    // Remove double-counting in forward associations
    for (LArClusterMergeMap::const_iterator iter1 = fullForwardMergeMap.begin(), iterEnd1 = fullForwardMergeMap.end(); iter1 != iterEnd1; ++iter1)
    {
        Cluster *pInnerCluster = iter1->first;
        const ClusterList &clusterMerges(iter1->second);

        for (ClusterList::iterator iter2 = clusterMerges.begin(), iterEnd2 = clusterMerges.end(); iter2 != iterEnd2; ++iter2)
        {
            Cluster *pOuterCluster = *iter2;

            if (pOuterCluster == pInnerCluster)
                throw pandora::StatusCodeException(STATUS_CODE_INVALID_PARAMETER);

            bool isNeighbouringCluster(true);

            for (ClusterList::iterator iter3 = clusterMerges.begin(), iterEnd3 = clusterMerges.end(); iter3 != iterEnd3; ++iter3)
            {
                Cluster *pMiddleCluster = *iter3;

                if (pMiddleCluster == pOuterCluster)
                    continue;

                if (fullForwardMergeMap[pMiddleCluster].count(pOuterCluster))
                {
                    isNeighbouringCluster = false;
                    break;
                }
            }

            if (isNeighbouringCluster)
	    {
	        if (forwardMergeMap[pInnerCluster].count(pOuterCluster) == 0)
                    forwardMergeMap[pInnerCluster].insert(pOuterCluster);

                if (backwardMergeMap[pOuterCluster].count(pInnerCluster) == 0)
                    backwardMergeMap[pOuterCluster].insert(pInnerCluster);
	    }
        }
    }

    // Remove double-counting in backward associations
    for (LArClusterMergeMap::const_iterator iter1 = fullBackwardMergeMap.begin(), iterEnd1 = fullBackwardMergeMap.end(); iter1 != iterEnd1; ++iter1)
    {
        Cluster *pOuterCluster = iter1->first;
        const ClusterList &clusterMerges(iter1->second);

        for (ClusterList::iterator iter2 = clusterMerges.begin(), iterEnd2 = clusterMerges.end(); iter2 != iterEnd2; ++iter2)
        {
            Cluster *pInnerCluster = *iter2;

            if (pInnerCluster == pOuterCluster)
                throw pandora::StatusCodeException(STATUS_CODE_INVALID_PARAMETER);

            bool isNeighbouringCluster(true);

            for (ClusterList::iterator iter3 = clusterMerges.begin(), iterEnd3 = clusterMerges.end(); iter3 != iterEnd3; ++iter3)
            {
                Cluster *pMiddleCluster = *iter3;

                if (pMiddleCluster == pInnerCluster)
                    continue;

                if (fullBackwardMergeMap[pMiddleCluster].count(pInnerCluster))
                {
                    isNeighbouringCluster = false;
                    break;
                }
            }

            if (isNeighbouringCluster)
	    {
                if (forwardMergeMap[pInnerCluster].count(pOuterCluster) == 0)
                    forwardMergeMap[pInnerCluster].insert(pOuterCluster);

                if (backwardMergeMap[pOuterCluster].count(pInnerCluster) == 0)
                    backwardMergeMap[pOuterCluster].insert(pInnerCluster);
	    }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

void TransverseAssociationAlgorithm::FillClusterAssociationMap(LArClusterMergeMap &forwardMergeMap, LArClusterMergeMap &backwardMergeMap,
    ClusterAssociationMap &clusterAssociationMap) const
{
    // Select neighbouring forward associations
    for (LArClusterMergeMap::const_iterator iter1 = forwardMergeMap.begin(), iterEnd1 = forwardMergeMap.end(); iter1 != iterEnd1; ++iter1)
    {
        Cluster *pInnerCluster = iter1->first;
        const ClusterList &clusterMerges(iter1->second);

        for (ClusterList::iterator iter2 = clusterMerges.begin(), iterEnd2 = clusterMerges.end(); iter2 != iterEnd2; ++iter2)
        {
            Cluster *pOuterCluster = *iter2;

            if (pOuterCluster == pInnerCluster)
                throw pandora::StatusCodeException(STATUS_CODE_INVALID_PARAMETER);

            clusterAssociationMap[pInnerCluster].m_forwardAssociations.insert(pOuterCluster);
            clusterAssociationMap[pOuterCluster].m_backwardAssociations.insert(pInnerCluster);
        }
    }

    // Select neighbouring backward associations
    for (LArClusterMergeMap::const_iterator iter1 = backwardMergeMap.begin(), iterEnd1 = backwardMergeMap.end(); iter1 != iterEnd1; ++iter1)
    {
        Cluster *pOuterCluster = iter1->first;
        const ClusterList &clusterMerges(iter1->second);

        for (ClusterList::iterator iter2 = clusterMerges.begin(), iterEnd2 = clusterMerges.end(); iter2 != iterEnd2; ++iter2)
        {
            Cluster *pInnerCluster = *iter2;

            if (pInnerCluster == pOuterCluster)
                throw pandora::StatusCodeException(STATUS_CODE_INVALID_PARAMETER);

            clusterAssociationMap[pInnerCluster].m_forwardAssociations.insert(pOuterCluster);
            clusterAssociationMap[pOuterCluster].m_backwardAssociations.insert(pInnerCluster);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

void TransverseAssociationAlgorithm::GetAssociatedClusters(Cluster *const pCluster, const ClusterVector &transverseVector,
    const ClusterVector &longitudinalVector, ClusterVector &associatedVector) const
{
    float windowMinX(-std::numeric_limits<float>::max());
    float windowMaxX(+std::numeric_limits<float>::max());

    float clusterMinX(-std::numeric_limits<float>::max());
    float clusterMaxX(+std::numeric_limits<float>::max());
    float clusterMinZ(-std::numeric_limits<float>::max());
    float clusterMaxZ(+std::numeric_limits<float>::max());

    this->GetExtremalCoordinatesX(pCluster, clusterMinX, clusterMaxX);
    this->GetExtremalCoordinatesZ(pCluster, clusterMinZ, clusterMaxZ);

    for (ClusterVector::const_iterator iter = longitudinalVector.begin(), iterEnd = longitudinalVector.end(); iter != iterEnd; ++iter)
    {
         Cluster *pLongitudinalCluster = *iter;  

         if (pCluster == pLongitudinalCluster)
            continue;

         float projectedX(0.f);

         try{  
	   this->GetProjectedCoordinateX(pLongitudinalCluster, clusterMinZ, projectedX);

           if (projectedX < clusterMinX)
	       windowMinX = std::max(windowMinX, projectedX);
           else if (projectedX > clusterMaxX)
	       windowMaxX = std::min(windowMaxX, projectedX);
	 }
         catch (StatusCodeException &)
	 {
	 }

         try{  
	   this->GetProjectedCoordinateX(pLongitudinalCluster, clusterMaxZ, projectedX);

           if (projectedX < clusterMinX)
	       windowMinX = std::max(windowMinX, projectedX);
           else if (projectedX > clusterMaxX)
	       windowMaxX = std::min(windowMaxX, projectedX);
	 }
         catch (StatusCodeException &)
	 {
	 }
    }

    for (ClusterVector::const_iterator iter = transverseVector.begin(), iterEnd = transverseVector.end(); iter != iterEnd; ++iter)
    {
        Cluster *pTransverseCluster = *iter;

        if (pCluster == pTransverseCluster)
            continue;

        this->GetExtremalCoordinatesX(pTransverseCluster, clusterMinX, clusterMaxX);

        if (clusterMinX > windowMaxX || clusterMaxX < windowMinX)
	    continue;

        if (this->IsTransverseAssociated(pCluster, pTransverseCluster))
            associatedVector.push_back(pTransverseCluster);
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

bool TransverseAssociationAlgorithm::IsTransverseAssociated(const Cluster *const pCluster1, const Cluster *const pCluster2) const
{
    const CartesianVector innerCentroid1(pCluster1->GetCentroid(pCluster1->GetInnerPseudoLayer()));
    const CartesianVector outerCentroid1(pCluster1->GetCentroid(pCluster1->GetOuterPseudoLayer()));
    const CartesianVector innerCentroid2(pCluster2->GetCentroid(pCluster2->GetInnerPseudoLayer()));
    const CartesianVector outerCentroid2(pCluster2->GetCentroid(pCluster2->GetOuterPseudoLayer()));

    const float averageX1(0.5f * (innerCentroid1.GetX() + outerCentroid1.GetX()));
    const float averageZ1(0.5f * (innerCentroid1.GetZ() + outerCentroid1.GetZ()));
    const float averageX2(0.5f * (innerCentroid2.GetX() + outerCentroid2.GetX()));
    const float averageZ2(0.5f * (innerCentroid2.GetZ() + outerCentroid2.GetZ()));

    if ((std::fabs(averageX2 - averageX1) < m_clusterWindow) && (std::fabs(averageZ2 - averageZ1) < m_clusterWindow) &&
        (std::fabs(averageZ2 - averageZ1) < std::fabs(averageX2 - averageX1) * std::fabs(m_clusterTanAngle)))
    {
        return true;
    }

    return false;
}
//------------------------------------------------------------------------------------------------------------------------------------------

bool TransverseAssociationAlgorithm::IsTransverseAssociated(const LArTransverseCluster *const pInnerTransverseCluster,
    const LArTransverseCluster *const pOuterTransverseCluster) const
{
    if (pInnerTransverseCluster->GetDirection().GetDotProduct(pOuterTransverseCluster->GetDirection()) < m_minCosRelativeAngle)
        return false;

    if (!this->IsTransverseAssociated(pInnerTransverseCluster, pOuterTransverseCluster->GetInnerVertex()))
        return false;

    if (!this->IsTransverseAssociated(pOuterTransverseCluster, pInnerTransverseCluster->GetOuterVertex()))
        return false;

    return true;
}

//------------------------------------------------------------------------------------------------------------------------------------------

bool TransverseAssociationAlgorithm::IsTransverseAssociated(const LArTransverseCluster *const pTransverseCluster, const pandora::CartesianVector& testVertex) const
{
    const CartesianVector &innerVertex(pTransverseCluster->GetInnerVertex());
    const CartesianVector &outerVertex(pTransverseCluster->GetOuterVertex());
    const CartesianVector &direction(pTransverseCluster->GetDirection());

    if (std::fabs(direction.GetCrossProduct(testVertex - innerVertex).GetMagnitudeSquared()) > m_maxTransverseSeparation * m_maxTransverseSeparation)
        return false;

    if ((direction.GetDotProduct(testVertex - innerVertex) < -m_clusterWindow) || (direction.GetDotProduct(testVertex - outerVertex) > +m_clusterWindow))
        return false;

    return true;
}

//------------------------------------------------------------------------------------------------------------------------------------------

float TransverseAssociationAlgorithm::GetTransverseLength(const Cluster *const pCentralCluster, const ClusterVector &associatedClusters) const
{
    float overallMinX(std::numeric_limits<float>::max());
    float overallMaxX(-std::numeric_limits<float>::max());

    this->GetExtremalCoordinatesX(pCentralCluster, overallMinX, overallMaxX);

    float localMinX(std::numeric_limits<float>::max());
    float localMaxX(-std::numeric_limits<float>::max());

    for (ClusterVector::const_iterator iter = associatedClusters.begin(), iterEnd = associatedClusters.end(); iter != iterEnd; ++iter)
    {
        Cluster *pAssociatedCluster = *iter;

        this->GetExtremalCoordinatesX(pAssociatedCluster, localMinX, localMaxX);

        if (localMinX < overallMinX)
	    overallMinX = localMinX; 

        if (localMaxX > overallMaxX)
	    overallMaxX = localMaxX;
    }

    return (overallMaxX - overallMinX);
}

//------------------------------------------------------------------------------------------------------------------------------------------

bool TransverseAssociationAlgorithm::IsExtremalCluster(const bool isForward, const Cluster *const pCurrentCluster,  const Cluster *const pTestCluster) const
{
    float currentMinX(0.f), currentMaxX(0.f);
    this->GetExtremalCoordinatesX(pCurrentCluster, currentMinX, currentMaxX);

    float testMinX(0.f), testMaxX(0.f);
    this->GetExtremalCoordinatesX(pTestCluster, testMinX, testMaxX);

    if (isForward)
    {
        return (testMaxX > currentMaxX);
    }
    else
    {
        return (testMinX < currentMinX); 
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

void TransverseAssociationAlgorithm::GetExtremalCoordinatesXZ(const Cluster *const pCluster, const bool useX, float &minXZ, float &maxXZ) const
{
    minXZ = +std::numeric_limits<float>::max();
    maxXZ = -std::numeric_limits<float>::max();

    const OrderedCaloHitList &orderedCaloHitList(pCluster->GetOrderedCaloHitList());

    for (OrderedCaloHitList::const_iterator iter = orderedCaloHitList.begin(), iterEnd = orderedCaloHitList.end(); iter != iterEnd; ++iter)
    {
        for (CaloHitList::const_iterator hitIter = iter->second->begin(), hitIterEnd = iter->second->end(); hitIter != hitIterEnd; ++hitIter)
        {
	    const float caloHitXZ(useX ? (*hitIter)->GetPositionVector().GetX() : (*hitIter)->GetPositionVector().GetZ());

            if (caloHitXZ < minXZ)
                minXZ = caloHitXZ;

            if (caloHitXZ > maxXZ)
                maxXZ = caloHitXZ;
        }
    } 

    if (maxXZ < minXZ)
        throw pandora::StatusCodeException(STATUS_CODE_FAILURE);
}

//------------------------------------------------------------------------------------------------------------------------------------------

void TransverseAssociationAlgorithm::GetExtremalCoordinatesX(const Cluster *const pCluster, float &minX, float &maxX) const
{
    return this->GetExtremalCoordinatesXZ(pCluster, true, minX, maxX);
}

//------------------------------------------------------------------------------------------------------------------------------------------

void TransverseAssociationAlgorithm::GetExtremalCoordinatesZ(const Cluster *const pCluster, float &minZ, float &maxZ) const
{
    return this->GetExtremalCoordinatesXZ(pCluster, false, minZ, maxZ);
}

//------------------------------------------------------------------------------------------------------------------------------------------

void TransverseAssociationAlgorithm::GetProjectedCoordinateX(const pandora::Cluster *const pCluster, const float &inputZ, float &outputX) const
{
    // Initial sanity check using inner/outer centroids
    const float minZ(pCluster->GetCentroid(pCluster->GetInnerPseudoLayer()).GetZ());
    const float maxZ(pCluster->GetCentroid(pCluster->GetOuterPseudoLayer()).GetZ());

    if (inputZ < minZ - m_maxLongitudinalDisplacement || inputZ > maxZ + m_maxLongitudinalDisplacement)
        throw pandora::StatusCodeException(STATUS_CODE_NOT_FOUND);

    // Now search for projected position
    float deltaZ(m_maxLongitudinalDisplacement);
    bool foundProjection(false);

    const OrderedCaloHitList &orderedCaloHitList(pCluster->GetOrderedCaloHitList());

    for (OrderedCaloHitList::const_iterator iter = orderedCaloHitList.begin(), iterEnd = orderedCaloHitList.end(); iter != iterEnd; ++iter)
    {
        for (CaloHitList::const_iterator hitIter = iter->second->begin(), hitIterEnd = iter->second->end(); hitIter != hitIterEnd; ++hitIter)
        {
	    const float newDeltaZ(std::fabs((*hitIter)->GetPositionVector().GetZ() - inputZ));

	    if (newDeltaZ < deltaZ)
	    {
	        deltaZ = newDeltaZ;
                outputX = (*hitIter)->GetPositionVector().GetX();
	        foundProjection = true;
	    }
	}
    }

    if (!foundProjection)
        throw pandora::StatusCodeException(STATUS_CODE_NOT_FOUND);
}

//------------------------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------------------------

TransverseAssociationAlgorithm::LArTransverseCluster::LArTransverseCluster(Cluster *pSeedCluster, const ClusterVector &associatedClusters) :
    m_pSeedCluster(pSeedCluster),
    m_associatedClusters(associatedClusters),
    m_innerVertex(0.f, 0.f, 0.f),
    m_outerVertex(0.f, 0.f, 0.f),
    m_direction(0.f, 0.f, 0.f)
{
    float Swzz(0.f), Swxx(0.f), Swzx(0.f), Swz(0.f), Swx(0.f), Sw(0.f);
    float minX(std::numeric_limits<float>::max());
    float maxX(-std::numeric_limits<float>::max());

    ClusterList clusterList;
    clusterList.insert(pSeedCluster);
    clusterList.insert(associatedClusters.begin(), associatedClusters.end());

    for (ClusterList::const_iterator iterI = clusterList.begin(), iterEndI = clusterList.end(); iterI != iterEndI; ++iterI)
    {
        for (OrderedCaloHitList::const_iterator iterJ = (*iterI)->GetOrderedCaloHitList().begin(), iterEndJ = (*iterI)->GetOrderedCaloHitList().end(); iterJ != iterEndJ; ++iterJ)
        {
            for (CaloHitList::const_iterator iterK = iterJ->second->begin(), iterEndK = iterJ->second->end(); iterK != iterEndK; ++iterK)
            {
                const CaloHit *pCaloHit = *iterK;

                if (pCaloHit->GetPositionVector().GetX() < minX)
                    minX = pCaloHit->GetPositionVector().GetX();

                if (pCaloHit->GetPositionVector().GetX() > maxX)
                    maxX = pCaloHit->GetPositionVector().GetX();

                Swzz += pCaloHit->GetPositionVector().GetZ() * pCaloHit->GetPositionVector().GetZ();
                Swxx += pCaloHit->GetPositionVector().GetX() * pCaloHit->GetPositionVector().GetX();
                Swzx += pCaloHit->GetPositionVector().GetZ() * pCaloHit->GetPositionVector().GetX();
                Swz  += pCaloHit->GetPositionVector().GetZ();
                Swx  += pCaloHit->GetPositionVector().GetX();
                Sw   += 1.f;
            }
        }
    }

    if (Sw > 0.f)
    {
        const float averageX(Swx / Sw);
        const float averageZ(Swz / Sw);

        if (Sw * Swxx - Swx * Swx > 0.f)
        {
            float m((Sw * Swzx - Swx * Swz) / (Sw * Swxx - Swx * Swx));
            float px(1.f / std::sqrt(1.f + m * m));
            float pz(m / std::sqrt(1.f + m * m));

            m_innerVertex.SetValues(minX, 0.f, averageZ + m * (minX - averageX));
            m_outerVertex.SetValues(maxX, 0.f, averageZ + m * (maxX - averageX));
            m_direction.SetValues(px, 0.f, pz);
        }
        else
        {
            m_innerVertex.SetValues(averageX, 0.f, averageZ);
            m_outerVertex.SetValues(averageX, 0.f, averageZ);
            m_direction.SetValues(1.f, 0.f, 0.f);
        }
    }
    else
    {
        throw StatusCodeException(STATUS_CODE_NOT_INITIALIZED);
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode TransverseAssociationAlgorithm::ReadSettings(const TiXmlHandle xmlHandle)
{
    m_clusterWindow = 3.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "ClusterWindow", m_clusterWindow));

    m_clusterAngle = 45.f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "clusterAngle", m_clusterAngle));

    m_clusterCosAngle = std::cos(m_clusterAngle * M_PI / 180.f);
    m_clusterTanAngle = std::tan(m_clusterAngle * M_PI / 180.f);

    m_minCosRelativeAngle = 0.866f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MinCosRelativeAngle", m_minCosRelativeAngle));

    m_maxTransverseSeparation = 1.5f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MaxTransverseSeparation", m_maxTransverseSeparation));

    m_minTransverseDisplacement = 1.5f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MinTransverseDisplacement", m_minTransverseDisplacement));

    m_maxLongitudinalDisplacement = 1.5f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MaxLongitudinalDisplacement", m_maxLongitudinalDisplacement));

    m_transverseClusterMaxLength = 7.5f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "TransverseClusterMaxLength", m_transverseClusterMaxLength));

    m_transverseClusterMaxCaloHits = 5;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "TransverseClusterMaxCaloHits", m_transverseClusterMaxCaloHits));

    m_longitudinalClusterMinLength = 5.0f;
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "LongitudinalClusterMinLength", m_longitudinalClusterMinLength));

    return ClusterAssociationAlgorithm::ReadSettings(xmlHandle);
}

} // namespace lar
