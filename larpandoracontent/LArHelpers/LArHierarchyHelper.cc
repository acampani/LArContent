/**
 *  @file   larpandoracontent/LArHelpers/LArHierarchyHelper.cc
 *
 *  @brief  Implementation of the lar hierarchy helper class.
 *
 *  $Log: $
 */

#include "Pandora/PdgTable.h"
#include "Pandora/StatusCodes.h"

#include "larpandoracontent/LArHelpers/LArHierarchyHelper.h"

namespace lar_content
{

using namespace pandora;

LArHierarchyHelper::MCHierarchy::MCHierarchy(const ReconstructabilityCriteria &recoCriteria) :
    m_recoCriteria(recoCriteria),
    m_pNeutrino{nullptr}
{
}

//------------------------------------------------------------------------------------------------------------------------------------------

LArHierarchyHelper::MCHierarchy::~MCHierarchy()
{
    for (const Node *pNode : m_rootNodes)
        delete pNode;
    m_rootNodes.clear();
}

//------------------------------------------------------------------------------------------------------------------------------------------

void LArHierarchyHelper::MCHierarchy::FillHierarchy(const MCParticleList &mcParticleList, const CaloHitList &caloHitList,
    const bool foldToPrimaries, const bool foldToLeadingShowers)
{
    auto predicate = [](const MCParticle *pMC) { return std::abs(pMC->GetParticleId() == NEUTRON); };
    m_mcToHitsMap.clear();
    for (const CaloHit *pCaloHit : caloHitList)
    {
        try
        {
            const MCParticle *pMC{MCParticleHelper::GetMainMCParticle(pCaloHit)};
            m_mcToHitsMap[pMC].emplace_back(pCaloHit);
        }
        catch(const StatusCodeException&)
        {
            std::cout << "Found calo hit with no MC" << std::endl;
        }
    }

    if (foldToPrimaries && !foldToLeadingShowers)
    {
        MCParticleSet primarySet;
        m_pNeutrino = LArHierarchyHelper::GetMCPrimaries(mcParticleList, primarySet);
        MCParticleList primaries(primarySet.begin(), primarySet.end());
        if (m_recoCriteria.m_removeNeutrons)
            primaries.erase(std::remove_if(primaries.begin(), primaries.end(), predicate), primaries.end());
        for (const MCParticle *pPrimary : primaries)
        {
            MCParticleList allParticles{pPrimary};
            if (!m_recoCriteria.m_removeNeutrons)
            {
                LArMCParticleHelper::GetAllDescendentMCParticles(pPrimary, allParticles);
            }
            else
            {   // Collect track-like and shower-like particles together, but throw out neutrons and descendents
                MCParticleList neutrons;
                LArMCParticleHelper::GetAllDescendentMCParticles(pPrimary, allParticles, allParticles, neutrons);
            }
            CaloHitList allHits;
            for (const MCParticle *pMC : allParticles)
            {
                // Not all MC particles will have hits
                if (m_mcToHitsMap.find(pMC) != m_mcToHitsMap.end())
                {
                    const CaloHitList &caloHits(m_mcToHitsMap.at(pMC));
                    allHits.insert(allHits.begin(), caloHits.begin(), caloHits.end());
                }
            }
            m_rootNodes.emplace_back(new Node(*this, allParticles, allHits));
        }
    }
    else if (foldToPrimaries && foldToLeadingShowers)
    {
        MCParticleSet primarySet;
        m_pNeutrino = LArHierarchyHelper::GetMCPrimaries(mcParticleList, primarySet);
        MCParticleList primaries(primarySet.begin(), primarySet.end());
        if (m_recoCriteria.m_removeNeutrons)
            primaries.erase(std::remove_if(primaries.begin(), primaries.end(), predicate), primaries.end());
        for (const MCParticle *pPrimary : primaries)
        {
            MCParticleList allParticles{pPrimary}, showerParticles, neutrons;
            int pdg{std::abs(pPrimary->GetParticleId())};
            const bool isShower{pdg == E_MINUS || pdg == PHOTON};
            const bool isNeutron{pdg == NEUTRON};
            if (isShower || isNeutron)
            {
                if (!m_recoCriteria.m_removeNeutrons)
                {
                    LArMCParticleHelper::GetAllDescendentMCParticles(pPrimary, allParticles);
                }
                else
                {   // Throw away neutrons
                    MCParticleList dummy;
                    LArMCParticleHelper::GetAllDescendentMCParticles(pPrimary, allParticles, allParticles, dummy);
                }
            }
            else
            {
                LArMCParticleHelper::GetAllDescendentMCParticles(pPrimary, allParticles, showerParticles, neutrons);
            }
            CaloHitList allHits;
            for (const MCParticle *pMC : allParticles)
            {
                // ATTN - Not all MC particles will have hits
                if (m_mcToHitsMap.find(pMC) != m_mcToHitsMap.end())
                {
                    const CaloHitList &caloHits(m_mcToHitsMap.at(pMC));
                    allHits.insert(allHits.begin(), caloHits.begin(), caloHits.end());
                }
            }
            Node *pNode{new Node(*this, allParticles, allHits)};
            m_rootNodes.emplace_back(pNode);
            if (!showerParticles.empty())
            {   // Collect up all descendent hits for each shower and add the nodes as a child of the root node
                for (const MCParticle *pChild : showerParticles)
                    pNode->FillFlat(pChild);
            }
            if (!m_recoCriteria.m_removeNeutrons && !neutrons.empty())
            {   // Collect up all descendent hits for each neutron and add the nodes as a child of the root node
                for (const MCParticle *pChild : neutrons)
                    pNode->FillFlat(pChild);
            }
        }
    }
    else if (foldToLeadingShowers)
    {
        // Identify the primaries as the starting point for the hierarchy
        // We have special handling for the neutrino, probably want something similar for testbeam particles
        MCParticleSet primarySet;
        m_pNeutrino = LArHierarchyHelper::GetMCPrimaries(mcParticleList, primarySet);
        MCParticleList primaries(primarySet.begin(), primarySet.end());
        if (m_recoCriteria.m_removeNeutrons)
            primaries.erase(std::remove_if(primaries.begin(), primaries.end(), predicate), primaries.end());
        for (const MCParticle *pPrimary : primaries)
        {
            MCParticleList allParticles{pPrimary};
            int pdg{std::abs(pPrimary->GetParticleId())};
            const bool isShower{pdg == E_MINUS || pdg == PHOTON};
            const bool isNeutron{pdg == NEUTRON};
            if (isShower || (isNeutron && !m_recoCriteria.m_removeNeutrons))
                LArMCParticleHelper::GetAllDescendentMCParticles(pPrimary, allParticles);
            CaloHitList allHits;
            for (const MCParticle *pMC : allParticles)
            {
                // ATTN - Not all MC particles will have hits
                if (m_mcToHitsMap.find(pMC) != m_mcToHitsMap.end())
                {
                    const CaloHitList &caloHits(m_mcToHitsMap.at(pMC));
                    allHits.insert(allHits.begin(), caloHits.begin(), caloHits.end());
                }
            }
            Node *pNode{new Node(*this, allParticles, allHits)};
            m_rootNodes.emplace_back(pNode);
            if (!(isShower || isNeutron))
            {   // Find the children of this particle and recursively add them to the hierarchy
                const MCParticleList &children{pPrimary->GetDaughterList()};
                for (const MCParticle *pChild : children)
                    pNode->FillHierarchy(pChild, foldToLeadingShowers);
            }
        }
    }
    else
    {
        MCParticleSet primarySet;
        m_pNeutrino = LArHierarchyHelper::GetMCPrimaries(mcParticleList, primarySet);
        MCParticleList primaries(primarySet.begin(), primarySet.end());
        if (m_recoCriteria.m_removeNeutrons)
            primaries.erase(std::remove_if(primaries.begin(), primaries.end(), predicate), primaries.end());
        for (const MCParticle *pPrimary : primaries)
        {
            MCParticleList allParticles{pPrimary};
            CaloHitList allHits;
            for (const MCParticle *pMC : allParticles)
            {
                // ATTN - Not all MC particles will have hits
                if (m_mcToHitsMap.find(pMC) != m_mcToHitsMap.end())
                {
                    const CaloHitList &caloHits(m_mcToHitsMap.at(pMC));
                    allHits.insert(allHits.begin(), caloHits.begin(), caloHits.end());
                }
            }
            Node *pNode{new Node(*this, allParticles, allHits)};
            m_rootNodes.emplace_back(pNode);
            // Find the children of this particle and recursively add them to the hierarchy
            const MCParticleList &children{pPrimary->GetDaughterList()};
            for (const MCParticle *pChild : children)
                pNode->FillHierarchy(pChild, foldToLeadingShowers);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

void LArHierarchyHelper::MCHierarchy::GetFlattenedNodes(NodeVector &nodeVector) const
{
    NodeList queue;
    for (const Node* pNode : m_rootNodes)
    {
        nodeVector.emplace_back(pNode);
        queue.emplace_back(pNode);
    }
    while (!queue.empty())
    {
        const NodeVector &children{queue.front()->GetChildren()};
        queue.pop_front();
        for (const Node *pChild : children)
        {
            nodeVector.emplace_back(pChild);
            queue.emplace_back(pChild);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

const std::string LArHierarchyHelper::MCHierarchy::ToString() const
{
    std::string str;
    for (const Node *pNode : m_rootNodes)
        str += pNode->ToString("") + "\n";

    return str;
}

//------------------------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------------------------

LArHierarchyHelper::MCHierarchy::Node::Node(const MCHierarchy &hierarchy, const MCParticle *pMC) :
    m_hierarchy(hierarchy),
    m_pdg{0}
{
    if (pMC)
    {
        m_pdg = pMC->GetParticleId();
        m_mcParticles.emplace_back(pMC);
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

LArHierarchyHelper::MCHierarchy::Node::Node(const MCHierarchy &hierarchy, const MCParticleList &mcParticleList, const CaloHitList &caloHitList) :
    m_hierarchy(hierarchy),
    m_pdg{0}
{
    if (!mcParticleList.empty())
        m_pdg = mcParticleList.front()->GetParticleId();
    m_mcParticles = mcParticleList; m_mcParticles.sort();
    m_caloHits = caloHitList; m_caloHits.sort();
}

//------------------------------------------------------------------------------------------------------------------------------------------

LArHierarchyHelper::MCHierarchy::Node::~Node()
{
    m_mcParticles.clear();
    m_caloHits.clear();
    for (const Node *node : m_children)
        delete node;
    m_children.clear();
}

//------------------------------------------------------------------------------------------------------------------------------------------

void LArHierarchyHelper::MCHierarchy::Node::FillHierarchy(const MCParticle *pRoot, const bool foldToLeadingShowers)
{
    MCParticleList allParticles{pRoot};
    int pdg{std::abs(pRoot->GetParticleId())};
    const bool isShower{pdg == E_MINUS || pdg == PHOTON};
    const bool isNeutron{pdg == NEUTRON};
    if (foldToLeadingShowers && (isShower || (isNeutron && !m_hierarchy.m_recoCriteria.m_removeNeutrons)))
        LArMCParticleHelper::GetAllDescendentMCParticles(pRoot, allParticles);
    else if (m_hierarchy.m_recoCriteria.m_removeNeutrons && isNeutron)
        return;
    CaloHitList allHits;
    for (const MCParticle *pMC : allParticles)
    {
        // ATTN - Not all MC particles will have hits
        if (m_hierarchy.m_mcToHitsMap.find(pMC) != m_hierarchy.m_mcToHitsMap.end())
        {
            const CaloHitList &caloHits(m_hierarchy.m_mcToHitsMap.at(pMC));
            allHits.insert(allHits.begin(), caloHits.begin(), caloHits.end());
        }
    }
    if (!allParticles.empty())
    {
        Node *pNode{new Node(m_hierarchy, allParticles, allHits)};
        m_children.emplace_back(pNode);
        if (!foldToLeadingShowers || (foldToLeadingShowers && !(isShower || isNeutron)))
        {   // Find the children of this particle and recursively add them to the hierarchy
            const MCParticleList &children{pRoot->GetDaughterList()};
            for (const MCParticle *pChild : children)
                pNode->FillHierarchy(pChild, foldToLeadingShowers);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

void LArHierarchyHelper::MCHierarchy::Node::FillFlat(const MCParticle *pRoot)
{
    MCParticleList allParticles{pRoot};
    if (!m_hierarchy.m_recoCriteria.m_removeNeutrons)
    {
        LArMCParticleHelper::GetAllDescendentMCParticles(pRoot, allParticles);
    }
    else
    {
        MCParticleList neutrons;
        LArMCParticleHelper::GetAllDescendentMCParticles(pRoot, allParticles, allParticles, neutrons);
    }
    CaloHitList allHits;
    for (const MCParticle *pMC : allParticles)
    {
        // ATTN - Not all MC particles will have hits
        if (m_hierarchy.m_mcToHitsMap.find(pMC) != m_hierarchy.m_mcToHitsMap.end())
        {
            const CaloHitList &caloHits(m_hierarchy.m_mcToHitsMap.at(pMC));
            allHits.insert(allHits.begin(), caloHits.begin(), caloHits.end());
        }
    }
    if (!allParticles.empty())
    {
        Node *pNode{new Node(m_hierarchy, allParticles, allHits)};
        m_children.emplace_back(pNode);
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

bool LArHierarchyHelper::MCHierarchy::Node::IsReconstructable() const
{
    const bool enoughHits{m_caloHits.size() >= m_hierarchy.m_recoCriteria.m_minHits};
    bool enoughGoodViews{false};
    int nHitsU{0}, nHitsV{0}, nHitsW{0};
    for (const CaloHit *pCaloHit : m_caloHits)
    {
        switch (pCaloHit->GetHitType())
        {
            case TPC_VIEW_U:
                ++nHitsU;
                break;
            case TPC_VIEW_V:
                ++nHitsV;
                break;
            case TPC_VIEW_W:
                ++nHitsW;
                break;
            default:
                break;
        }
        int nGoodViews{0};
        if (nHitsU >= m_hierarchy.m_recoCriteria.m_minHitsForGoodView)
            ++nGoodViews;
        if (nHitsV >= m_hierarchy.m_recoCriteria.m_minHitsForGoodView)
            ++nGoodViews;
        if (nHitsW >= m_hierarchy.m_recoCriteria.m_minHitsForGoodView)
            ++nGoodViews;
        if (nGoodViews >= m_hierarchy.m_recoCriteria.m_minGoodViews)
        {
            enoughGoodViews = true;
            break;
        }
    }

    return enoughHits && enoughGoodViews;
}

//------------------------------------------------------------------------------------------------------------------------------------------

const MCParticleList &LArHierarchyHelper::MCHierarchy::Node::GetMCParticles() const
{
    return m_mcParticles;
}

//------------------------------------------------------------------------------------------------------------------------------------------

const CaloHitList &LArHierarchyHelper::MCHierarchy::Node::GetCaloHits() const
{
    return m_caloHits;
}

//------------------------------------------------------------------------------------------------------------------------------------------

int LArHierarchyHelper::MCHierarchy::Node::GetParticleId() const
{
    return m_pdg;
}

//------------------------------------------------------------------------------------------------------------------------------------------

const std::string LArHierarchyHelper::MCHierarchy::Node::ToString(const std::string &prefix) const
{
    std::string str(prefix + "PDG: " + std::to_string(m_pdg) + " Energy: " + std::to_string(m_mcParticles.front()->GetEnergy()) +
        " Hits: " + std::to_string(m_caloHits.size()) + "\n");
    for (const Node *pChild : m_children)
        str += pChild->ToString(prefix + "   ");

    return str;
}

//------------------------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------------------------

LArHierarchyHelper::MCHierarchy::ReconstructabilityCriteria::ReconstructabilityCriteria() :
    m_minHits{15},
    m_minHitsForGoodView{5},
    m_minGoodViews{2},
    m_removeNeutrons{true}
{
}

//------------------------------------------------------------------------------------------------------------------------------------------

LArHierarchyHelper::MCHierarchy::ReconstructabilityCriteria::ReconstructabilityCriteria(const ReconstructabilityCriteria &obj) :
    m_minHits{obj.m_minHits},
    m_minHitsForGoodView{obj.m_minHitsForGoodView},
    m_minGoodViews{obj.m_minGoodViews},
    m_removeNeutrons{obj.m_removeNeutrons}
{
}

//------------------------------------------------------------------------------------------------------------------------------------------

LArHierarchyHelper::MCHierarchy::ReconstructabilityCriteria::ReconstructabilityCriteria(const unsigned int minHits,
    const unsigned int minHitsForGoodView, const unsigned int minGoodViews, const bool removeNeutrons) :
    m_minHits{minHits},
    m_minHitsForGoodView{minHitsForGoodView},
    m_minGoodViews{minGoodViews},
    m_removeNeutrons{removeNeutrons}
{
}

//------------------------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------------------------

LArHierarchyHelper::RecoHierarchy::RecoHierarchy() :
    m_pNeutrino{nullptr}
{
}

//------------------------------------------------------------------------------------------------------------------------------------------

LArHierarchyHelper::RecoHierarchy::~RecoHierarchy()
{
    for (const Node *pNode : m_rootNodes)
        delete pNode;
    m_rootNodes.clear();
}

//------------------------------------------------------------------------------------------------------------------------------------------

void LArHierarchyHelper::RecoHierarchy::FillHierarchy(const PfoList &pfoList, const bool foldToPrimaries, const bool foldToLeadingShowers)
{
    if (foldToPrimaries && !foldToLeadingShowers)
    {
        PfoSet primaries;
        m_pNeutrino = LArHierarchyHelper::GetRecoPrimaries(pfoList, primaries);
        for (const ParticleFlowObject *pPrimary : primaries)
        {
            PfoList allParticles;
            // ATTN - pPrimary gets added to the list of downstream PFOs, not just the child PFOs
            LArPfoHelper::GetAllDownstreamPfos(pPrimary, allParticles);
            CaloHitList allHits;
            for (const ParticleFlowObject *pPfo : allParticles)
            {
                LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_U, allHits);
                LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_V, allHits);
                LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_W, allHits);
                LArPfoHelper::GetIsolatedCaloHits(pPfo, TPC_VIEW_U, allHits);
                LArPfoHelper::GetIsolatedCaloHits(pPfo, TPC_VIEW_V, allHits);
                LArPfoHelper::GetIsolatedCaloHits(pPfo, TPC_VIEW_W, allHits);
            }
            m_rootNodes.emplace_back(new Node(*this, allParticles, allHits));
        }
    }
    else if (foldToPrimaries && foldToLeadingShowers)
    {
        PfoSet primaries;
        m_pNeutrino = LArHierarchyHelper::GetRecoPrimaries(pfoList, primaries);
        for (const ParticleFlowObject *pPrimary : primaries)
        {
            PfoList allParticles, showerParticles;
            int pdg{std::abs(pPrimary->GetParticleId())};
            const bool isShower{pdg == E_MINUS};
            // ATTN - pPrimary gets added to the list of downstream PFOs, not just the child PFOs
            if (isShower)
                LArPfoHelper::GetAllDownstreamPfos(pPrimary, allParticles);
            else
                LArPfoHelper::GetAllDownstreamPfos(pPrimary, allParticles, showerParticles);
            CaloHitList allHits;
            for (const ParticleFlowObject *pPfo : allParticles)
            {
                LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_U, allHits);
                LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_V, allHits);
                LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_W, allHits);
                LArPfoHelper::GetIsolatedCaloHits(pPfo, TPC_VIEW_U, allHits);
                LArPfoHelper::GetIsolatedCaloHits(pPfo, TPC_VIEW_V, allHits);
                LArPfoHelper::GetIsolatedCaloHits(pPfo, TPC_VIEW_W, allHits);
            }
            Node *pNode{new Node(*this, allParticles, allHits)};
            m_rootNodes.emplace_back(pNode);
            if (!showerParticles.empty())
            {   // Collect up all descendent hits for each shower and add the nodes as a child of the root node
                for (const ParticleFlowObject *pChild : showerParticles)
                    pNode->FillFlat(pChild);
            }
        }
    }
    else if (foldToLeadingShowers)
    {
        // Identify the primaries as the starting point for the hierarchy
        // We have special handling for the neutrino, probably want something similar for testbeam particles
        PfoSet primaries;
        m_pNeutrino = LArHierarchyHelper::GetRecoPrimaries(pfoList, primaries);
        for (const ParticleFlowObject *pPrimary : primaries)
        {
            PfoList allParticles;
            int pdg{std::abs(pPrimary->GetParticleId())};
            const bool isShower{pdg == E_MINUS};
            // ATTN - pPrimary gets added to the list of downstream PFOs, not just the child PFOs
            if (isShower)
                LArPfoHelper::GetAllDownstreamPfos(pPrimary, allParticles);
            else
                allParticles.emplace_back(pPrimary);

            CaloHitList allHits;
            for (const ParticleFlowObject *pPfo : allParticles)
            {
                LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_U, allHits);
                LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_V, allHits);
                LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_W, allHits);
                LArPfoHelper::GetIsolatedCaloHits(pPfo, TPC_VIEW_U, allHits);
                LArPfoHelper::GetIsolatedCaloHits(pPfo, TPC_VIEW_V, allHits);
                LArPfoHelper::GetIsolatedCaloHits(pPfo, TPC_VIEW_W, allHits);
            }
            Node *pNode{new Node(*this, allParticles, allHits)};
            m_rootNodes.emplace_back(pNode);
            if (!isShower)
            {   // Find the children of this particle and recursively add them to the hierarchy
                const PfoList &children{pPrimary->GetDaughterPfoList()};
                for (const ParticleFlowObject *pChild : children)
                    pNode->FillHierarchy(pChild, foldToLeadingShowers);
            }
        }
    }
    else
    {
        PfoSet primaries;
        m_pNeutrino = LArHierarchyHelper::GetRecoPrimaries(pfoList, primaries);
        for (const ParticleFlowObject *pPrimary : primaries)
        {
            PfoList allParticles{pPrimary};
            CaloHitList allHits;
            for (const ParticleFlowObject *pPfo : allParticles)
            {
                LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_U, allHits);
                LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_V, allHits);
                LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_W, allHits);
                LArPfoHelper::GetIsolatedCaloHits(pPfo, TPC_VIEW_U, allHits);
                LArPfoHelper::GetIsolatedCaloHits(pPfo, TPC_VIEW_V, allHits);
                LArPfoHelper::GetIsolatedCaloHits(pPfo, TPC_VIEW_W, allHits);
            }
            Node *pNode{new Node(*this, allParticles, allHits)};
            m_rootNodes.emplace_back(pNode);
            // Find the children of this particle and recursively add them to the hierarchy
            const PfoList &children{pPrimary->GetDaughterPfoList()};
            for (const ParticleFlowObject *pChild : children)
                pNode->FillHierarchy(pChild, foldToLeadingShowers);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

void LArHierarchyHelper::RecoHierarchy::GetFlattenedNodes(NodeVector &nodeVector) const
{
    NodeList queue;
    for (const Node* pNode : m_rootNodes)
    {
        nodeVector.emplace_back(pNode);
        queue.emplace_back(pNode);
    }
    while (!queue.empty())
    {
        const NodeVector &children{queue.front()->GetChildren()};
        queue.pop_front();
        for (const Node *pChild : children)
        {
            nodeVector.emplace_back(pChild);
            queue.emplace_back(pChild);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

const std::string LArHierarchyHelper::RecoHierarchy::ToString() const
{
    std::string str;
    for (const Node *pNode : m_rootNodes)
        str += pNode->ToString("") + "\n";

    return str;
}

//------------------------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------------------------

LArHierarchyHelper::RecoHierarchy::Node::Node(const RecoHierarchy &hierarchy, const ParticleFlowObject *pPfo) :
    m_hierarchy(hierarchy),
    m_pdg{0}
{
    if (pPfo)
    {
        m_pdg = pPfo->GetParticleId();
        m_pfos.emplace_back(pPfo);
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

LArHierarchyHelper::RecoHierarchy::Node::Node(const RecoHierarchy &hierarchy, const PfoList &pfoList, const CaloHitList &caloHitList) :
    m_hierarchy(hierarchy),
    m_pdg{0}
{
    if (!pfoList.empty())
        m_pdg = pfoList.front()->GetParticleId();
    m_pfos = pfoList; m_pfos.sort();
    m_caloHits = caloHitList; m_caloHits.sort();
}

//------------------------------------------------------------------------------------------------------------------------------------------

LArHierarchyHelper::RecoHierarchy::Node::~Node()
{
    m_pfos.clear();
    m_caloHits.clear();
    for (const Node *node : m_children)
        delete node;
    m_children.clear();
}

//------------------------------------------------------------------------------------------------------------------------------------------

void LArHierarchyHelper::RecoHierarchy::Node::FillHierarchy(const ParticleFlowObject *pRoot, const bool foldToLeadingShowers)
{
    PfoList allParticles;
    int pdg{std::abs(pRoot->GetParticleId())};
    const bool isShower{pdg == E_MINUS};
    if (foldToLeadingShowers && isShower)
        LArPfoHelper::GetAllDownstreamPfos(pRoot, allParticles);
    else
        allParticles.emplace_back(pRoot);

    CaloHitList allHits;
    for (const ParticleFlowObject *pPfo : allParticles)
    {
        LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_U, allHits);
        LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_V, allHits);
        LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_W, allHits);
        LArPfoHelper::GetIsolatedCaloHits(pPfo, TPC_VIEW_U, allHits);
        LArPfoHelper::GetIsolatedCaloHits(pPfo, TPC_VIEW_V, allHits);
        LArPfoHelper::GetIsolatedCaloHits(pPfo, TPC_VIEW_W, allHits);
    }
    Node *pNode{new Node(m_hierarchy, allParticles, allHits)};
    m_children.emplace_back(pNode);
    if (!foldToLeadingShowers || (foldToLeadingShowers && !isShower))
    {   // Find the children of this particle and recursively add them to the hierarchy
        const PfoList &children{pRoot->GetDaughterPfoList()};
        for (const ParticleFlowObject *pChild : children)
            pNode->FillHierarchy(pChild, foldToLeadingShowers);
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

void LArHierarchyHelper::RecoHierarchy::Node::FillFlat(const ParticleFlowObject *pRoot)
{
    PfoList allParticles;
    LArPfoHelper::GetAllDownstreamPfos(pRoot, allParticles);
    CaloHitList allHits;
    for (const ParticleFlowObject *pPfo : allParticles)
    {
        LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_U, allHits);
        LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_V, allHits);
        LArPfoHelper::GetCaloHits(pPfo, TPC_VIEW_W, allHits);
        LArPfoHelper::GetIsolatedCaloHits(pPfo, TPC_VIEW_U, allHits);
        LArPfoHelper::GetIsolatedCaloHits(pPfo, TPC_VIEW_V, allHits);
        LArPfoHelper::GetIsolatedCaloHits(pPfo, TPC_VIEW_W, allHits);
    }
    Node *pNode{new Node(m_hierarchy, allParticles, allHits)};
    m_children.emplace_back(pNode);
}

//------------------------------------------------------------------------------------------------------------------------------------------

const PfoList &LArHierarchyHelper::RecoHierarchy::Node::GetRecoParticles() const
{
    return m_pfos;
}

//------------------------------------------------------------------------------------------------------------------------------------------

const CaloHitList &LArHierarchyHelper::RecoHierarchy::Node::GetCaloHits() const
{
    return m_caloHits;
}

//------------------------------------------------------------------------------------------------------------------------------------------

int LArHierarchyHelper::RecoHierarchy::Node::GetParticleId() const
{
    return m_pdg;
}

//------------------------------------------------------------------------------------------------------------------------------------------

const std::string LArHierarchyHelper::RecoHierarchy::Node::ToString(const std::string &prefix) const
{
    std::string str(prefix + "PDG: " + std::to_string(m_pdg) + " Hits: " + std::to_string(m_caloHits.size()) + "\n");
    for (const Node *pChild : m_children)
        str += pChild->ToString(prefix + "   ");

    return str;
}

//------------------------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------------------------

LArHierarchyHelper::MCMatches::MCMatches(const MCHierarchy::Node *pMC) :
    m_pMC{pMC}
{
}

//------------------------------------------------------------------------------------------------------------------------------------------

void LArHierarchyHelper::MCMatches::AddRecoMatch(const RecoHierarchy::Node *pReco, const int nSharedHits)
{
    m_recoNodes.emplace_back(pReco);
    m_sharedHits.emplace_back(nSharedHits);
}

//------------------------------------------------------------------------------------------------------------------------------------------

int LArHierarchyHelper::MCMatches::GetSharedHits(const RecoHierarchy::Node *pReco) const
{
    auto iter{std::find(m_recoNodes.begin(), m_recoNodes.end(), pReco)};
    if (iter == m_recoNodes.end())
        throw StatusCodeException(STATUS_CODE_NOT_FOUND);
    int index = iter - m_recoNodes.begin();

    return static_cast<int>(m_sharedHits[index]);
}

//------------------------------------------------------------------------------------------------------------------------------------------

float LArHierarchyHelper::MCMatches::GetPurity(const RecoHierarchy::Node *pReco) const
{
    auto iter{std::find(m_recoNodes.begin(), m_recoNodes.end(), pReco)};
    if (iter == m_recoNodes.end())
        throw StatusCodeException(STATUS_CODE_NOT_FOUND);
    int index = iter - m_recoNodes.begin();

    return m_sharedHits[index] / static_cast<float>(pReco->GetCaloHits().size());
}

//------------------------------------------------------------------------------------------------------------------------------------------

float LArHierarchyHelper::MCMatches::GetCompleteness(const RecoHierarchy::Node *pReco) const
{
    auto iter{std::find(m_recoNodes.begin(), m_recoNodes.end(), pReco)};
    if (iter == m_recoNodes.end())
        throw StatusCodeException(STATUS_CODE_NOT_FOUND);
    int index = iter - m_recoNodes.begin();

    return m_sharedHits[index] / static_cast<float>(m_pMC->GetCaloHits().size());
}

//------------------------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------------------------

void LArHierarchyHelper::FillMCHierarchy(const MCParticleList &mcParticleList, const CaloHitList &caloHitList, const bool foldToPrimaries,
    const bool foldToLeadingShowers, MCHierarchy &hierarchy)
{
    hierarchy.FillHierarchy(mcParticleList, caloHitList, foldToPrimaries, foldToLeadingShowers);
}

//------------------------------------------------------------------------------------------------------------------------------------------

void LArHierarchyHelper::FillRecoHierarchy(const PfoList &pfoList, const bool foldToPrimaries, const bool foldToLeadingShowers,
    RecoHierarchy &hierarchy)
{
    hierarchy.FillHierarchy(pfoList, foldToPrimaries, foldToLeadingShowers);
}

//------------------------------------------------------------------------------------------------------------------------------------------

void LArHierarchyHelper::MatchHierarchies(const MCHierarchy &mcHierarchy, const RecoHierarchy &recoHierarchy, MCMatchesVector &matchVector)
{
    MCHierarchy::NodeVector mcNodes;
    mcHierarchy.GetFlattenedNodes(mcNodes);
    RecoHierarchy::NodeVector recoNodes;
    recoHierarchy.GetFlattenedNodes(recoNodes);

    std::sort(mcNodes.begin(), mcNodes.end(), [](const MCHierarchy::Node *lhs, const MCHierarchy::Node *rhs)
        {
            return lhs->GetCaloHits().size() > rhs->GetCaloHits().size();
        });
    std::sort(recoNodes.begin(), recoNodes.end(), [](const RecoHierarchy::Node *lhs, const RecoHierarchy::Node *rhs)
        {
            return lhs->GetCaloHits().size() > rhs->GetCaloHits().size();
        });

    std::map<const MCHierarchy::Node *, MCMatches> mcToMatchMap;
    for (const RecoHierarchy::Node *pRecoNode : recoNodes)
    {
        const CaloHitList &recoHits{pRecoNode->GetCaloHits()};
        const MCHierarchy::Node *pBestNode{nullptr};
        size_t bestSharedHits{0};
        for (const MCHierarchy::Node *pMCNode : mcNodes)
        {
            if (!pMCNode->IsReconstructable())
                continue;
            const CaloHitList &mcHits{pMCNode->GetCaloHits()};
            CaloHitVector intersection;
            std::set_intersection(mcHits.begin(), mcHits.end(), recoHits.begin(), recoHits.end(), std::back_inserter(intersection));

            if (!intersection.empty())
            {
                const size_t sharedHits{intersection.size()};
                if (sharedHits > bestSharedHits)
                {
                    bestSharedHits = sharedHits;
                    pBestNode = pMCNode;
                }
            }
        }
        if (pBestNode)
        {
            auto iter{mcToMatchMap.find(pBestNode)};
            if (iter != mcToMatchMap.end())
            {
                MCMatches &match(iter->second);
                match.AddRecoMatch(pRecoNode, static_cast<int>(bestSharedHits));
            }
            else
            {
                MCMatches match(pBestNode);
                match.AddRecoMatch(pRecoNode, static_cast<int>(bestSharedHits));
                mcToMatchMap.insert(std::make_pair(pBestNode, match));
            }
        }
    }

    for (auto [ mc, matches ] : mcToMatchMap)
    {   (void)mc;
        matchVector.emplace_back(matches);
    }

    for (const MCHierarchy::Node *pMCNode : mcNodes)
    {
        if (mcToMatchMap.find(pMCNode) == mcToMatchMap.end())
        {   // Unmatched MC
            MCMatches match(pMCNode);
            matchVector.emplace_back(match);
        }
    }

    auto predicate = [](const MCMatches &lhs, const MCMatches &rhs)
        {
            return lhs.GetMC()->GetCaloHits().size() > rhs.GetMC()->GetCaloHits().size();
        };
    std::sort(matchVector.begin(), matchVector.end(), predicate);

    for (const MCMatches &match : matchVector)
    {
        const MCHierarchy::Node *pMCNode{match.GetMC()};
        const int pdg{pMCNode->GetParticleId()};
        const size_t mcHits{pMCNode->GetCaloHits().size()};
        std::cout << "MC " << pdg << " hits " << mcHits << std::endl;
        const RecoHierarchy::NodeVector &nodeVector{match.GetRecoMatches()};
        for (const RecoHierarchy::Node *pRecoNode : nodeVector)
        {
            const int recoHits{static_cast<int>(pRecoNode->GetCaloHits().size())};
            const int sharedHits{match.GetSharedHits(pRecoNode)};
            const float purity{match.GetPurity(pRecoNode)};
            const float completeness{match.GetCompleteness(pRecoNode)};
            std::cout << "   Matched " << sharedHits << " out of " << recoHits << " with purity " << purity << " and completeness " <<
                completeness << std::endl;
        }
        if (nodeVector.empty())
            std::cout << "   Unmatched" << std::endl;
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------
// private

const MCParticle *LArHierarchyHelper::GetMCPrimaries(const MCParticleList &mcParticleList, MCParticleSet &primaries)
{
    const MCParticle *pRoot{nullptr};
    for (const MCParticle *pMC : mcParticleList)
    {
        try
        {
            const MCParticle *const pPrimary{LArMCParticleHelper::GetPrimaryMCParticle(pMC)};
            primaries.insert(pPrimary);
        }
        catch (const StatusCodeException&)
        {
            if (LArMCParticleHelper::IsNeutrino(pMC))
                pRoot = pMC;
            else if (pMC->GetParticleId() != 111)
                std::cout << "LArHierarchyHelper::MCHierarchy::FillHierarchy: MC particle with PDG code " << pMC->GetParticleId() <<
                    " at address " << pMC << " has no associated primary particle" << std::endl;
        }
    }

    return pRoot;
}

const ParticleFlowObject *LArHierarchyHelper::GetRecoPrimaries(const PfoList &pfoList, PfoSet &primaries)
{
    const ParticleFlowObject *pRoot{nullptr};
    for (const ParticleFlowObject *pPfo : pfoList)
    {
        if (LArPfoHelper::IsNeutrino(pPfo))
        {
            pRoot = pPfo;
            break;
        }
        else
        {
            const ParticleFlowObject *const pParent{LArPfoHelper::GetParentPfo(pPfo)};
            if (LArPfoHelper::IsNeutrino(pParent))
            {
                pRoot = pParent;
                break;
            }
            else
            {
                std::cout << "Still need to handle test beam, cosmics, ..." << std::endl;
            }
        }
    }
    const PfoList &children{pRoot->GetDaughterPfoList()};
    for (const ParticleFlowObject *pPrimary : children)
        primaries.insert(pPrimary); 

    return pRoot;
}

} // namespace lar_content
