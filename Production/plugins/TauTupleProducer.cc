/*! Creates tuple for tau analysis.
*/

#include "FWCore/Framework/interface/EDAnalyzer.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "CommonTools/UtilAlgos/interface/TFileService.h"
#include "DataFormats/PatCandidates/interface/Tau.h"
#include "SimDataFormats/GeneratorProducts/interface/GenEventInfoProduct.h"
#include "AnalysisDataFormats/TopObjects/interface/TtGenEvent.h"
#include "DataFormats/PatCandidates/interface/PackedCandidate.h"
#include "RecoTauTag/RecoTau/interface/PFRecoTauClusterVariables.h"
#include "DataFormats/PatCandidates/interface/Electron.h"
#include "DataFormats/PatCandidates/interface/Muon.h"

#include "AnalysisTools/Core/include/Tools.h"
#include "AnalysisTools/Core/include/TextIO.h"
#include "TauML/Analysis/include/TauTuple.h"
#include "TauML/Production/include/GenTruthTools.h"
#include "TauML/Analysis/include/TauIdResults.h"

namespace {

namespace MuonSubdetId {
enum { DT = 1, CSC = 2, RPC = 3, GEM = 4, ME0 = 5 };
}

template<typename LVector1, typename LVector2>
float dEta(const LVector1& p4, const LVector2& tau_p4)
{
    return static_cast<float>(p4.eta() - tau_p4.eta());
}

template<typename LVector1, typename LVector2>
float dPhi(const LVector1& p4, const LVector2& tau_p4)
{
    return static_cast<float>(ROOT::Math::VectorUtil::DeltaPhi(p4, tau_p4));
}

struct MuonHitMatch {
    static constexpr int n_muon_stations = 4;

    std::map<int, std::vector<UInt_t>> n_matches, n_hits;
    unsigned n_muons{0};
    const pat::Muon* best_matched_muon{nullptr};
    double deltaR2_best_match{-1};

    MuonHitMatch()
    {
        n_matches[MuonSubdetId::DT].assign(n_muon_stations, 0);
        n_matches[MuonSubdetId::CSC].assign(n_muon_stations, 0);
        n_matches[MuonSubdetId::RPC].assign(n_muon_stations, 0);
        n_hits[MuonSubdetId::DT].assign(n_muon_stations, 0);
        n_hits[MuonSubdetId::CSC].assign(n_muon_stations, 0);
        n_hits[MuonSubdetId::RPC].assign(n_muon_stations, 0);
    }

    void AddMatchedMuon(const pat::Muon& muon, const pat::Tau& tau)
    {
        static constexpr int n_stations = 4;

        ++n_muons;
        const double dR2 = ROOT::Math::VectorUtil::DeltaR2(tau.p4(), muon.p4());
        if(!best_matched_muon || dR2 < deltaR2_best_match) {
            best_matched_muon = &muon;
            deltaR2_best_match = dR2;
        }

        for(const auto& segment : muon.matches()) {
            if(segment.segmentMatches.empty()) continue;
            if(n_matches.count(segment.detector()))
                ++n_matches.at(segment.detector()).at(segment.station() - 1);
        }

        if(muon.outerTrack().isNonnull()) {
            const auto& hit_pattern = muon.outerTrack()->hitPattern();
            for(int hit_index = 0; hit_index < hit_pattern.numberOfAllHits(reco::HitPattern::TRACK_HITS);
                ++hit_index) {
                auto hit_id = hit_pattern.getHitPattern(reco::HitPattern::TRACK_HITS, hit_index);
                if(hit_id == 0) break;
                if(hit_pattern.muonHitFilter(hit_id) && (hit_pattern.getHitType(hit_id) == TrackingRecHit::valid
                                                         || hit_pattern.getHitType(hit_id == TrackingRecHit::bad))) {
                    const int station = hit_pattern.getMuonStation(hit_id) - 1;
                    if(station > 0 && station < n_stations) {
                        std::vector<UInt_t>* muon_n_hits = nullptr;
                        if(hit_pattern.muonDTHitFilter(hit_id))
                            muon_n_hits = &n_hits.at(MuonSubdetId::DT);
                        else if(hit_pattern.muonCSCHitFilter(hit_id))
                            muon_n_hits = &n_hits.at(MuonSubdetId::CSC);
                        else if(hit_pattern.muonRPCHitFilter(hit_id))
                            muon_n_hits = &n_hits.at(MuonSubdetId::RPC);

                        if(muon_n_hits)
                            ++muon_n_hits->at(station);
                    }
                }
            }
        }
    }

    static std::vector<const pat::Muon*> FindMatchedMuons(const pat::Tau& tau, const pat::MuonCollection& muons,
                                                           double deltaR, double minPt)
    {
        const reco::Muon* hadr_cand_muon = nullptr;
        if(tau.leadPFChargedHadrCand().isNonnull() && tau.leadPFChargedHadrCand()->muonRef().isNonnull())
            hadr_cand_muon = tau.leadPFChargedHadrCand()->muonRef().get();
        std::vector<const pat::Muon*> matched_muons;
        const double deltaR2 = std::pow(deltaR, 2);
        for(const pat::Muon& muon : muons) {
            const reco::Muon* reco_muon = &muon;
            if(muon.pt() <= minPt) continue;
            if(reco_muon == hadr_cand_muon) continue;
            if(ROOT::Math::VectorUtil::DeltaR2(tau.p4(), muon.p4()) >= deltaR2) continue;
            matched_muons.push_back(&muon);
        }
        return matched_muons;
    }

     void FillTuple(tau_tuple::Tau& tau, const pat::Tau& reco_tau) const
     {
         static constexpr float default_value = tau_tuple::DefaultFillValue<float>();

         tau.n_matched_muons = n_muons;
         tau.muon_pt = best_matched_muon != nullptr ? best_matched_muon->p4().pt() : default_value;
         tau.muon_dEta = best_matched_muon != nullptr ? dEta(best_matched_muon->p4(), reco_tau.p4()) : default_value;
         tau.muon_dPhi = best_matched_muon != nullptr ? dPhi(best_matched_muon->p4(), reco_tau.p4()) : default_value;
         tau.muon_n_matches_DT_1 = n_matches.at(MuonSubdetId::DT).at(0);
         tau.muon_n_matches_DT_2 = n_matches.at(MuonSubdetId::DT).at(1);
         tau.muon_n_matches_DT_3 = n_matches.at(MuonSubdetId::DT).at(2);
         tau.muon_n_matches_DT_4 = n_matches.at(MuonSubdetId::DT).at(3);
         tau.muon_n_matches_CSC_1 = n_matches.at(MuonSubdetId::CSC).at(0);
         tau.muon_n_matches_CSC_2 = n_matches.at(MuonSubdetId::CSC).at(1);
         tau.muon_n_matches_CSC_3 = n_matches.at(MuonSubdetId::CSC).at(2);
         tau.muon_n_matches_CSC_4 = n_matches.at(MuonSubdetId::CSC).at(3);
         tau.muon_n_matches_RPC_1 = n_matches.at(MuonSubdetId::RPC).at(0);
         tau.muon_n_matches_RPC_2 = n_matches.at(MuonSubdetId::RPC).at(1);
         tau.muon_n_matches_RPC_3 = n_matches.at(MuonSubdetId::RPC).at(2);
         tau.muon_n_matches_RPC_4 = n_matches.at(MuonSubdetId::RPC).at(3);
         tau.muon_n_hits_DT_1 = n_hits.at(MuonSubdetId::DT).at(0);
         tau.muon_n_hits_DT_2 = n_hits.at(MuonSubdetId::DT).at(1);
         tau.muon_n_hits_DT_3 = n_hits.at(MuonSubdetId::DT).at(2);
         tau.muon_n_hits_DT_4 = n_hits.at(MuonSubdetId::DT).at(3);
         tau.muon_n_hits_CSC_1 = n_hits.at(MuonSubdetId::CSC).at(0);
         tau.muon_n_hits_CSC_2 = n_hits.at(MuonSubdetId::CSC).at(1);
         tau.muon_n_hits_CSC_3 = n_hits.at(MuonSubdetId::CSC).at(2);
         tau.muon_n_hits_CSC_4 = n_hits.at(MuonSubdetId::CSC).at(3);
         tau.muon_n_hits_RPC_1 = n_hits.at(MuonSubdetId::RPC).at(0);
         tau.muon_n_hits_RPC_2 = n_hits.at(MuonSubdetId::RPC).at(1);
         tau.muon_n_hits_RPC_3 = n_hits.at(MuonSubdetId::RPC).at(2);
         tau.muon_n_hits_RPC_4 = n_hits.at(MuonSubdetId::RPC).at(3);
         tau.muon_n_stations_with_matches_03 = CountMuonStationsWithMatches(0, 3);
         tau.muon_n_stations_with_hits_23 = CountMuonStationsWithHits(2, 3);
     }

private:
    unsigned CountMuonStationsWithMatches(size_t first_station, size_t last_station) const
    {
        static const std::map<int, std::vector<bool>> masks = {
            { MuonSubdetId::DT, { false, false, false, false } },
            { MuonSubdetId::CSC, { true, false, false, false } },
            { MuonSubdetId::RPC, { false, false, false, false } },
        };
        unsigned cnt = 0;
        for(unsigned n = first_station; n <= last_station; ++n) {
            for(const auto& match : n_matches) {
                if(!masks.at(match.first).at(n) && match.second.at(n) > 0) ++cnt;
            }
        }
        return cnt;
    }

    unsigned CountMuonStationsWithHits(size_t first_station, size_t last_station) const
    {
        static const std::map<int, std::vector<bool>> masks = {
            { MuonSubdetId::DT, { false, false, false, false } },
            { MuonSubdetId::CSC, { false, false, false, false } },
            { MuonSubdetId::RPC, { false, false, false, false } },
        };

        unsigned cnt = 0;
        for(unsigned n = first_station; n <= last_station; ++n) {
            for(const auto& hit : n_hits) {
                if(!masks.at(hit.first).at(n) && hit.second.at(n) > 0) ++cnt;
            }
        }
        return cnt;
    }
};
}


class TauTupleProducer : public edm::EDAnalyzer {
public:
    TauTupleProducer(const edm::ParameterSet& cfg) :
        isMC(cfg.getParameter<bool>("isMC")),
        saveGenTopInfo(cfg.getParameter<bool>("saveGenTopInfo")),
        genEvent_token(mayConsume<GenEventInfoProduct>(cfg.getParameter<edm::InputTag>("genEvent"))),
        topGenEvent_token(mayConsume<TtGenEvent>(cfg.getParameter<edm::InputTag>("topGenEvent"))),
        genParticles_token(consumes<std::vector<reco::GenParticle>>(cfg.getParameter<edm::InputTag>("genParticles"))),
        puInfo_token(mayConsume<std::vector<PileupSummaryInfo>>(cfg.getParameter<edm::InputTag>("puInfo"))),
        vertices_token(mayConsume<std::vector<reco::Vertex> >(cfg.getParameter<edm::InputTag>("vertices"))),
        rho_token(consumes<double>(cfg.getParameter<edm::InputTag>("rho"))),
        electrons_token(mayConsume<pat::ElectronCollection>(cfg.getParameter<edm::InputTag>("electrons"))),
        muons_token(mayConsume<pat::MuonCollection>(cfg.getParameter<edm::InputTag>("muons"))),
        taus_token(mayConsume<pat::TauCollection>(cfg.getParameter<edm::InputTag>("taus"))),
        tauTuple("taus", &edm::Service<TFileService>()->file(), false)
    {
    }

private:
    virtual void analyze(const edm::Event& event, const edm::EventSetup&) override
    {
        static constexpr float default_value = tau_tuple::DefaultFillValue<float>();

        tauTuple().run  = event.id().run();
        tauTuple().lumi = event.id().luminosityBlock();
        tauTuple().evt  = event.id().event();

        edm::Handle<std::vector<reco::Vertex>> vertices;
        event.getByToken(vertices_token, vertices);
        tauTuple().npv = vertices->size();
        edm::Handle<double> rho;
        event.getByToken(rho_token, rho);
        tauTuple().rho = *rho;

        if(isMC) {
            edm::Handle<GenEventInfoProduct> genEvent;
            event.getByToken(genEvent_token, genEvent);

            edm::Handle<std::vector<PileupSummaryInfo>> puInfo;
            event.getByToken(puInfo_token, puInfo);
            tauTuple().npu = analysis::gen_truth::GetNumberOfPileUpInteractions(puInfo);
            tauTuple().genEventWeight = genEvent->weight();

            if(saveGenTopInfo) {
                edm::Handle<TtGenEvent> topGenEvent;
                event.getByToken(topGenEvent_token, topGenEvent);
                if(topGenEvent.isValid()) {
                    analysis::GenEventType genEventType = analysis::GenEventType::Other;
                    if(topGenEvent->isFullHadronic())
                        genEventType = analysis::GenEventType::TTbar_Hadronic;
                    else if(topGenEvent->isSemiLeptonic())
                        genEventType = analysis::GenEventType::TTbar_SemiLeptonic;
                    else if(topGenEvent->isFullLeptonic())
                        genEventType = analysis::GenEventType::TTbar_Leptonic;
                    tauTuple().genEventType = static_cast<int>(genEventType);
                }
            }
        }

        edm::Handle<pat::ElectronCollection> electrons;
        event.getByToken(electrons_token, electrons);

        edm::Handle<pat::MuonCollection> muons;
        event.getByToken(muons_token, muons);

        edm::Handle<pat::TauCollection> taus;
        event.getByToken(taus_token, taus);

        unsigned tau_index = 0;
        for(const pat::Tau& tau : *taus) {
            static const bool id_names_printed = PrintTauIdNames(tau);
            (void)id_names_printed;

            tauTuple().tau_index = tau_index++;
            tauTuple().pt = tau.p4().pt();
            tauTuple().eta = tau.p4().eta();
            tauTuple().phi = tau.p4().phi();
            tauTuple().mass = tau.p4().mass();
            tauTuple().charge = tau.charge();

            if(isMC) {
                edm::Handle<std::vector<reco::GenParticle>> genParticles;
                event.getByToken(genParticles_token, genParticles);
                const auto match = analysis::gen_truth::LeptonGenMatch(tau.p4(), *genParticles);
                tauTuple().gen_match = static_cast<int>(match.first);
                tauTuple().gen_pt = match.second ? match.second->p4().pt() : default_value;
                tauTuple().gen_eta = match.second ? match.second->p4().eta() : default_value;
                tauTuple().gen_phi = match.second ? match.second->p4().phi() : default_value;
                tauTuple().gen_mass = match.second ? match.second->p4().mass() : default_value;

                static constexpr double minGenVisPt = 10;
                static const double dRmatch = 0.3;
                static const std::vector<int> pdgIdsGenElectron = { -11, 11 };
                static const std::vector<int> pdgIdsGenMuon = { -13, 13 };
                static const std::vector<int> pdgIdsGenQuarkOrGluon = { -6, -5, -4, -3, -2, -1, 1, 2, 3, 4, 5, 6, 21 };

                double dRmin;
                auto gen_ele = FindMatchingGenParticle(tau.p4(), *genParticles, minGenVisPt, pdgIdsGenElectron,
                                                       dRmatch, dRmin);
                tauTuple().has_gen_ele_match = gen_ele != nullptr;
                tauTuple().gen_ele_match_dR = gen_ele != nullptr ? dRmin : default_value;
                tauTuple().gen_ele_pdg = gen_ele != nullptr ? gen_ele->pdgId() : 0;
                tauTuple().gen_ele_pt = gen_ele != nullptr ? gen_ele->p4().pt() : default_value;
                tauTuple().gen_ele_eta = gen_ele != nullptr ? gen_ele->p4().eta() : default_value;
                tauTuple().gen_ele_phi = gen_ele != nullptr ? gen_ele->p4().phi() : default_value;
                tauTuple().gen_ele_mass = gen_ele != nullptr ? gen_ele->p4().mass() : default_value;

                auto gen_muon = FindMatchingGenParticle(tau.p4(), *genParticles, minGenVisPt, pdgIdsGenMuon,
                                                       dRmatch, dRmin);
                tauTuple().has_gen_muon_match = gen_muon != nullptr;
                tauTuple().gen_muon_match_dR = gen_muon != nullptr ? dRmin : default_value;
                tauTuple().gen_muon_pdg = gen_muon != nullptr ? gen_muon->pdgId() : 0;
                tauTuple().gen_muon_pt = gen_muon != nullptr ? gen_muon->p4().pt() : default_value;
                tauTuple().gen_muon_eta = gen_muon != nullptr ? gen_muon->p4().eta() : default_value;
                tauTuple().gen_muon_phi = gen_muon != nullptr ? gen_muon->p4().phi() : default_value;
                tauTuple().gen_muon_mass = gen_muon != nullptr ? gen_muon->p4().mass() : default_value;

                auto gen_qg = FindMatchingGenParticle(tau.p4(), *genParticles, minGenVisPt, pdgIdsGenMuon,
                                                       dRmatch, dRmin);
                tauTuple().has_gen_qg_match = gen_qg != nullptr;
                tauTuple().gen_qg_match_dR = gen_qg != nullptr ? dRmin : default_value;
                tauTuple().gen_qg_pdg = gen_qg != nullptr ? gen_qg->pdgId() : 0;
                tauTuple().gen_qg_pt = gen_qg != nullptr ? gen_qg->p4().pt() : default_value;
                tauTuple().gen_qg_eta = gen_qg != nullptr ? gen_qg->p4().eta() : default_value;
                tauTuple().gen_qg_phi = gen_qg != nullptr ? gen_qg->p4().phi() : default_value;
                tauTuple().gen_qg_mass = gen_qg != nullptr ? gen_qg->p4().mass() : default_value;
            }

            tauTuple().decayMode = tau.decayMode();
            tauTuple().id_flags = CreateTauIdResults(tau).GetResultBits();
            FillRawTauIds(tau);
            FillExtendedVariables(tau, *electrons, *muons);
            FillComponents(tau);

            tauTuple.Fill();
        }
    }

    virtual void endJob() override
    {
        tauTuple.Write();
    }

private:
    static bool PrintTauIdNames(const pat::Tau& tau)
    {
        static const std::string header(40, '-');

        std::set<std::string> tauId_names;
        for(const auto& id : tau.tauIDs())
            tauId_names.insert(id.first);
        std::cout << "Tau IDs:\n" << header << "\n";
        for(const std::string& name : tauId_names)
            std::cout << name << "\n";
        std::cout << header << std::endl;

        return true;
    }

    static analysis::TauIdResults CreateTauIdResults(const pat::Tau& tau)
    {
        analysis::TauIdResults results;
        const auto& descs = analysis::TauIdResults::GetResultDescriptors();
        for(size_t n = 0; n < descs.size(); ++n)
            results.SetResult(n, tau.tauID(descs.at(n).ToString()) > .5f);
        return results;
    }

    void FillRawTauIds(const pat::Tau& tau)
    {
#define VAR(type, name) tauTuple().name = tau.tauID(#name);
        RAW_TAU_IDS()
#undef VAR
    }

    void FillExtendedVariables(const pat::Tau& tau, const pat::ElectronCollection& electrons,
                               const pat::MuonCollection& muons)
    {
        static constexpr float default_value = tau_tuple::DefaultFillValue<float>();
        auto leadChargedHadrCand = dynamic_cast<const pat::PackedCandidate*>(tau.leadChargedHadrCand().get());

        tauTuple().dxy = tau.dxy();
        tauTuple().dxy_sig = tau.dxy_Sig();
        tauTuple().dz = leadChargedHadrCand ? leadChargedHadrCand->dz() : default_value;
        tauTuple().ip3d = tau.ip3d();
        tauTuple().ip3d_sig = tau.ip3d_Sig();
        tauTuple().hasSecondaryVertex = tau.hasSecondaryVertex();
        tauTuple().flightLength_r = tau.flightLength().R();
        tauTuple().flightLength_dEta = dEta(tau.flightLength(), tau.p4());
        tauTuple().flightLength_dPhi = dPhi(tau.flightLength(), tau.p4());
        tauTuple().flightLength_sig = tau.flightLengthSig();

        tauTuple().leadChargedHadrCand_pt = leadChargedHadrCand ? leadChargedHadrCand->p4().Pt() : default_value;
        tauTuple().leadChargedHadrCand_dEta = leadChargedHadrCand
                ? dEta(leadChargedHadrCand->p4(), tau.p4()) : default_value;
        tauTuple().leadChargedHadrCand_dPhi = leadChargedHadrCand
                ? dPhi(leadChargedHadrCand->p4(), tau.p4()) : default_value;
        tauTuple().leadChargedHadrCand_mass = leadChargedHadrCand ? leadChargedHadrCand->p4().mass() : default_value;

        tauTuple().pt_weighted_deta_strip = clusterVariables.tau_pt_weighted_deta_strip(tau, tau.decayMode());
        tauTuple().pt_weighted_dphi_strip = clusterVariables.tau_pt_weighted_dphi_strip(tau, tau.decayMode());
        tauTuple().pt_weighted_dr_signal = clusterVariables.tau_pt_weighted_dr_signal(tau, tau.decayMode());
        tauTuple().pt_weighted_dr_iso = clusterVariables.tau_pt_weighted_dr_iso(tau, tau.decayMode());
        tauTuple().leadingTrackNormChi2 = tau.leadingTrackNormChi2();
        tauTuple().e_ratio = clusterVariables.tau_Eratio(tau);
        tauTuple().gj_angle_diff = CalculateGottfriedJacksonAngleDifference(tau);
        tauTuple().n_photons = clusterVariables.tau_n_photons_total(tau);

        tauTuple().emFraction = tau.emFraction_MVA();
        CalculateEtaPhiAtEcalEntrance(tau, tauTuple().etaAtEcalEntrance, tauTuple().phiAtEcalEntrance);
        tauTuple().inside_ecal_crack = IsInEcalCrack(tau.p4().Eta());
        tauTuple().ecal_crack_dPhi = CalculateDeltaPhiCrack(tauTuple().etaAtEcalEntrance, tauTuple().phiAtEcalEntrance);
        tauTuple().ecal_crack_dEta = CalculateDeltaEtaCrack(tauTuple().etaAtEcalEntrance);

        tauTuple().has_gsf_track = leadChargedHadrCand && std::abs(leadChargedHadrCand->pdgId()) == 11;
        auto gsf_ele = FindMatchedElectron(tau, electrons, 0.3);
        tauTuple().gsf_ele_matched = gsf_ele != nullptr;
        tauTuple().gsf_ele_pt = gsf_ele != nullptr ? gsf_ele->p4().Pt() : default_value;
        tauTuple().gsf_ele_dEta = gsf_ele != nullptr ? dEta(gsf_ele->p4(), tau.p4()) : default_value;
        tauTuple().gsf_ele_dPhi = gsf_ele != nullptr ? dPhi(gsf_ele->p4(), tau.p4()) : default_value;
        tauTuple().gsf_ele_mass = gsf_ele != nullptr ? gsf_ele->p4().mass() : default_value;
        CalculateElectronClusterVars(gsf_ele, tauTuple().gsf_ele_Ee, tauTuple().gsf_ele_Egamma);
        tauTuple().gsf_ele_Pin = gsf_ele != nullptr ? gsf_ele->trackMomentumAtVtx().R() : default_value;
        tauTuple().gsf_ele_Pout = gsf_ele != nullptr ? gsf_ele->trackMomentumOut().R() : default_value;
        tauTuple().gsf_ele_EtotOverPin = tauTuple().gsf_ele_Pin > 0
                ? (tauTuple().gsf_ele_Ee + tauTuple().gsf_ele_Egamma) / tauTuple().gsf_ele_Pin : default_value;
        tauTuple().gsf_ele_Eecal = gsf_ele != nullptr ? gsf_ele->ecalEnergy() : default_value;
        tauTuple().gsf_ele_dEta_SeedClusterTrackAtCalo = gsf_ele != nullptr
                ? gsf_ele->deltaEtaSeedClusterTrackAtCalo() : default_value;
        tauTuple().gsf_ele_dPhi_SeedClusterTrackAtCalo = gsf_ele != nullptr
                ? gsf_ele->deltaPhiSeedClusterTrackAtCalo() : default_value;
        tauTuple().gsf_ele_mvaIn_sigmaEtaEta = gsf_ele != nullptr ? gsf_ele->mvaInput().sigmaEtaEta : default_value;
        tauTuple().gsf_ele_mvaIn_hadEnergy = gsf_ele != nullptr ? gsf_ele->mvaInput().hadEnergy : default_value;
        tauTuple().gsf_ele_mvaIn_deltaEta = gsf_ele != nullptr ? gsf_ele->mvaInput().deltaEta : default_value;
        tauTuple().gsf_ele_Chi2NormGSF = default_value;
        tauTuple().gsf_ele_GSFNumHits = default_value;
        tauTuple().gsf_ele_GSFTrackResol = default_value;
        tauTuple().gsf_ele_GSFTracklnPt = default_value;
        if(gsf_ele != nullptr && gsf_ele->gsfTrack().isNonnull()) {
            tauTuple().gsf_ele_Chi2NormGSF = gsf_ele->gsfTrack()->normalizedChi2();
            tauTuple().gsf_ele_GSFNumHits = gsf_ele->gsfTrack()->numberOfValidHits();
            if(gsf_ele->gsfTrack()->pt() > 0) {
                tauTuple().gsf_ele_GSFTrackResol = gsf_ele->gsfTrack()->ptError() / gsf_ele->gsfTrack()->pt();
                tauTuple().gsf_ele_GSFTracklnPt = std::log10(gsf_ele->gsfTrack()->pt());
            }
        }

        tauTuple().gsf_ele_Chi2NormKF = default_value;
        tauTuple().gsf_ele_KFNumHits = default_value;
        if(gsf_ele != nullptr && gsf_ele->closestCtfTrackRef().isNonnull()) {
            tauTuple().gsf_ele_Chi2NormKF = gsf_ele->closestCtfTrackRef()->normalizedChi2();
            tauTuple().gsf_ele_KFNumHits = gsf_ele->closestCtfTrackRef()->numberOfValidHits();
        }

        tauTuple().leadChargedCand_etaAtEcalEntrance = tau.etaAtEcalEntranceLeadChargedCand();
        tauTuple().leadChargedCand_pt = tau.ptLeadChargedCand();

        tauTuple().leadChargedHadrCand_HoP = default_value;
        tauTuple().leadChargedHadrCand_EoP = default_value;
        if(tau.leadChargedHadrCand()->pt() > 0) {
            tauTuple().leadChargedHadrCand_HoP = tau.hcalEnergyLeadChargedHadrCand() / tau.leadChargedHadrCand()->pt();
            tauTuple().leadChargedHadrCand_EoP = tau.ecalEnergyLeadChargedHadrCand() / tau.leadChargedHadrCand()->pt();
        }

        MuonHitMatch muon_hit_match;
        if(tau.leadPFChargedHadrCand().isNonnull() && tau.leadPFChargedHadrCand()->muonRef().isNonnull())
            muon_hit_match.AddMatchedMuon(*tau.leadPFChargedHadrCand()->muonRef(), tau);

        auto matched_muons = muon_hit_match.FindMatchedMuons(tau, muons, 0.3, 5);
        for(auto muon : matched_muons)
            muon_hit_match.AddMatchedMuon(*muon, tau);
        muon_hit_match.FillTuple(tauTuple(), tau);

        tauTuple().leadTrack_p = default_value;
        if(tau.leadPFChargedHadrCand().isNonnull()) {
            tauTuple().energy_ECAL = tau.leadPFChargedHadrCand()->ecalEnergy();
            tauTuple().energy_HCAL = tau.leadPFChargedHadrCand()->hcalEnergy();
            const reco::Track* leadTrack = nullptr;
            if(tau.leadPFChargedHadrCand()->trackRef().isNonnull())
                leadTrack = tau.leadPFChargedHadrCand()->trackRef().get();
            else if(tau.leadPFChargedHadrCand()->gsfTrackRef().isNonnull())
                leadTrack = tau.leadPFChargedHadrCand()->gsfTrackRef().get();
            if(leadTrack)
                tauTuple().leadTrack_p = leadTrack->p();
        } else {
            tauTuple().energy_ECAL = default_value;
            tauTuple().energy_HCAL = default_value;
        }
    }

#define SET_P4(branch, cand_p4, n) \
    tauTuple().branch##_pt = n != 0 ? cand_p4.Pt() : default_value; \
    tauTuple().branch##_dEta = n != 0 ? dEta(cand_p4, tau.p4()) : default_value; \
    tauTuple().branch##_dPhi = n != 0 ? dPhi(cand_p4, tau.p4()) : default_value; \
    tauTuple().branch##_mass = n != 0 ? cand_p4.mass() : default_value

#define ADD_P4(branch, cand_p4) \
    tauTuple().branch##_pt.push_back(cand_p4.Pt()); \
    tauTuple().branch##_dEta.push_back(dEta(cand_p4, tau.p4())); \
    tauTuple().branch##_dPhi.push_back(dPhi(cand_p4, tau.p4())); \
    tauTuple().branch##_mass.push_back(cand_p4.mass())

#define PRE_PROCESS_SIG_CAND(branch) \
    analysis::LorentzVectorXYZ branch##_sumIn(0, 0, 0, 0), branch##_sumOut(0, 0, 0, 0); \
    tauTuple().branch##_nTotal_innerSigCone = 0; \
    tauTuple().branch##_nTotal_outerSigCone = 0; \
    /**/

#define PROCESS_SIG_CAND(branch) \
    const double dR = ROOT::Math::VectorUtil::DeltaR(cand->p4(), tau.leadChargedHadrCand()->p4()); \
    const bool isInside_innerSigCone = dR < innerSigCone_radius; \
    tauTuple().branch##_isInside_innerSigCone.push_back(isInside_innerSigCone); \
    ADD_P4(branch, cand->p4()); \
    if(isInside_innerSigCone) { \
        branch##_sumIn += cand->p4(); \
        ++tauTuple().branch##_nTotal_innerSigCone; \
    } else { \
        branch##_sumOut += cand->p4(); \
        ++tauTuple().branch##_nTotal_outerSigCone; \
    } \
    /**/

#define POST_PROCESS_SIG_CAND(branch) \
    SET_P4(branch##_sum_innerSigCone, branch##_sumIn, tauTuple().branch##_nTotal_innerSigCone); \
    SET_P4(branch##_sum_outerSigCone, branch##_sumOut, tauTuple().branch##_nTotal_outerSigCone); \
    /**/

#define PRE_PROCESS_ISO_CAND(branch) \
    analysis::LorentzVectorXYZ branch##_sum(0, 0, 0, 0); \
    tauTuple().branch##_nTotal = 0; \
    /**/

#define PROCESS_ISO_CAND(branch) \
    ADD_P4(branch, cand->p4()); \
    branch##_sum += cand->p4(); \
    ++tauTuple().branch##_nTotal; \
    /**/

#define POST_PROCESS_ISO_CAND(branch) \
    SET_P4(branch##_sum, branch##_sum, tauTuple().branch##_nTotal); \
    /**/

    void FillComponents(const pat::Tau& tau)
    {
        static constexpr float default_value = tau_tuple::DefaultFillValue<float>();
        const double innerSigCone_radius = GetInnerSignalConeRadius(tau.pt());

        PRE_PROCESS_SIG_CAND(signalChargedHadrCands)
        for(const auto& cand : tau.signalChargedHadrCands()) {
            PROCESS_SIG_CAND(signalChargedHadrCands)
        }
        POST_PROCESS_SIG_CAND(signalChargedHadrCands)

        PRE_PROCESS_SIG_CAND(signalNeutrHadrCands)
        for(const auto& cand : tau.signalNeutrHadrCands()) {
            PROCESS_SIG_CAND(signalNeutrHadrCands)
        }
        POST_PROCESS_SIG_CAND(signalNeutrHadrCands)

        PRE_PROCESS_SIG_CAND(signalGammaCands)
        for(const auto& cand : tau.signalGammaCands()) {
            PROCESS_SIG_CAND(signalGammaCands)
        }
        POST_PROCESS_SIG_CAND(signalGammaCands)

        PRE_PROCESS_ISO_CAND(isolationChargedHadrCands)
        for(const auto& cand : tau.isolationChargedHadrCands()) {
            PROCESS_ISO_CAND(isolationChargedHadrCands)
        }
        POST_PROCESS_ISO_CAND(isolationChargedHadrCands)

        PRE_PROCESS_ISO_CAND(isolationNeutrHadrCands)
        for(const auto& cand : tau.isolationNeutrHadrCands()) {
            PROCESS_ISO_CAND(isolationNeutrHadrCands)
        }
        POST_PROCESS_ISO_CAND(isolationNeutrHadrCands)

        PRE_PROCESS_ISO_CAND(isolationGammaCands)
        for(const auto& cand : tau.isolationGammaCands()) {
            PROCESS_ISO_CAND(isolationGammaCands)
        }
        POST_PROCESS_ISO_CAND(isolationGammaCands)

        tauTuple().tau_visMass_innerSigCone = (signalGammaCands_sumIn + signalChargedHadrCands_sumIn).mass();
    }

#undef SET_P4
#undef ADD_P4
#undef PRE_PROCESS_SIG_CAND
#undef PROCESS_SIG_CAND
#undef POST_PROCESS_SIG_CAND
#undef PRE_PROCESS_ISO_CAND
#undef PROCESS_ISO_CAND
#undef POST_PROCESS_ISO_CAND

    static double GetInnerSignalConeRadius(double pt)
    {
        return std::max(.05, std::min(.1, 3./std::max(1., pt)));
    }

    // Copied from https://github.com/cms-sw/cmssw/blob/CMSSW_9_4_X/RecoTauTag/RecoTau/plugins/PATTauDiscriminationByMVAIsolationRun2.cc#L218
    static float CalculateGottfriedJacksonAngleDifference(const pat::Tau& tau)
    {
        if(tau.decayMode() == 10) {
            static constexpr double mTau = 1.77682;
            const double mAOne = tau.p4().M();
            const double pAOneMag = tau.p();
            const double argumentThetaGJmax = (std::pow(mTau,2) - std::pow(mAOne,2) ) / ( 2 * mTau * pAOneMag );
            const double argumentThetaGJmeasured = tau.p4().Vect().Dot(tau.flightLength())
                    / ( pAOneMag * tau.flightLength().R() );
            if ( std::abs(argumentThetaGJmax) <= 1. && std::abs(argumentThetaGJmeasured) <= 1. ) {
                double thetaGJmax = std::asin( argumentThetaGJmax );
                double thetaGJmeasured = std::acos( argumentThetaGJmeasured );
                return thetaGJmeasured - thetaGJmax;
            }
        }
        return tau_tuple::DefaultFillValue<float>();
    }

    static void CalculateEtaPhiAtEcalEntrance(const pat::Tau& tau, float& eta, float& phi)
    {
        float sumEtaTimesEnergy = 0., sumPhiTimesEnergy = 0., sumEnergy = 0.;
        for(const auto& pfCandidate : tau.signalPFCands()) {
            sumEtaTimesEnergy += pfCandidate->positionAtECALEntrance().eta() * pfCandidate->energy();
            sumPhiTimesEnergy += pfCandidate->positionAtECALEntrance().phi() * pfCandidate->energy();
            sumEnergy += pfCandidate->energy();
        }
        if(sumEnergy > 0) {
            eta = sumEtaTimesEnergy / sumEnergy;
            phi = sumPhiTimesEnergy / sumEnergy;
        } else {
            eta = tau_tuple::DefaultFillValue<float>();
            phi = tau_tuple::DefaultFillValue<float>();
        }
    }

    static bool IsInEcalCrack(double eta)
    {
        const double abs_eta = std::abs(eta);
        return abs_eta > 1.46 && abs_eta < 1.558;
    }

    static double AbsMin(double a, double b)
    {
        return std::abs(b) < std::abs(a) ? b : a;
    }

    // Copied from https://github.com/cms-sw/cmssw/blob/CMSSW_9_4_X/RecoTauTag/RecoTau/src/AntiElectronIDMVA6.cc#L1268
    // Compute the (unsigned) distance to the closest phi-crack in the ECAL barrel
    static double CalculateDeltaPhiCrack(double eta, double phi)
    {
        static constexpr double pi = M_PI;
        static constexpr double delta_cPhi = 0.00638; // IN: shift of this location if eta < 0

        // IN: define locations of the 18 phi-cracks
        static const auto fill_cPhi = [&]() {
            std::array<double, 18> c_phi;
            c_phi[0] = 2.97025;
            for ( unsigned iCrack = 1; iCrack <= 17; ++iCrack )
                c_phi[iCrack] = c_phi[0] - 2.*iCrack*pi/18;
            return c_phi;
        };
        static const std::array<double, 18> cPhi = fill_cPhi();

        double retVal = 99.;
        if ( eta >= -1.47464 && eta <= 1.47464 ) {
            // the location is shifted
            if ( eta < 0. ) phi += delta_cPhi;

            // CV: need to bring-back phi into interval [-pi,+pi]
            if ( phi >  pi ) phi -= 2.*pi;
            if ( phi < -pi ) phi += 2.*pi;

            if ( phi >= -pi && phi <= pi ) {
                // the problem of the extrema:
                if ( phi < cPhi[17] || phi >= cPhi[0] ) {
                    if ( phi < 0. ) phi += 2.*pi;
                    retVal = AbsMin(phi - cPhi[0], phi - cPhi[17] - 2.*pi);
                } else {
                    // between these extrema...
                    bool OK = false;
                    unsigned iCrack = 16;
                    while( !OK ) {
                        if ( phi < cPhi[iCrack] ) {
                            retVal = AbsMin(phi - cPhi[iCrack + 1], phi - cPhi[iCrack]);
                            OK = true;
                        } else {
                            iCrack -= 1;
                        }
                    }
                }
            } else {
                retVal = 0.; // IN: if there is a problem, we assume that we are in a crack
            }
        } else {
            return -99.;
        }

        return std::abs(retVal);
    }


    // Copied from https://github.com/cms-sw/cmssw/blob/CMSSW_9_4_X/RecoTauTag/RecoTau/src/AntiElectronIDMVA6.cc#L1317
    // Compute the (unsigned) distance to the closest eta-crack in the ECAL barrel
    static double CalculateDeltaEtaCrack(double eta)
    {
        // IN: define locations of the eta-cracks
        static constexpr double cracks[5] = { 0., 4.44747e-01, 7.92824e-01, 1.14090e+00, 1.47464e+00 };

        double retVal = 99.;
        for ( int iCrack = 0; iCrack < 5 ; ++iCrack ) {
            double d = AbsMin(eta - cracks[iCrack], eta + cracks[iCrack]);
            if ( std::abs(d) < std::abs(retVal) ) {
                retVal = d;
            }
        }
        return std::abs(retVal);
    }

    static const pat::Electron* FindMatchedElectron(const pat::Tau& tau, const pat::ElectronCollection& electrons,
                                                    double deltaR)
    {
        const double deltaR2 = std::pow(deltaR, 2);
        const pat::Electron* matched_ele = nullptr;
        for(const auto& ele : electrons) {
            if(ROOT::Math::VectorUtil::DeltaR2(tau.p4(), ele.p4()) < deltaR2 &&
                    (!matched_ele || matched_ele->pt() < ele.pt())) {
                matched_ele = &ele;
            }
        }
        return matched_ele;
    }

    static void CalculateElectronClusterVars(const pat::Electron* ele, float& elecEe, float& elecEgamma)
    {
        if(ele) {
            elecEe = elecEgamma = 0;
            auto superCluster = ele->superCluster();
            if(superCluster.isNonnull() && superCluster.isAvailable() && superCluster->clusters().isNonnull()
                    && superCluster->clusters().isAvailable()) {
                for(auto iter = superCluster->clustersBegin(); iter != superCluster->clustersEnd(); ++iter) {
                    const double energy = (*iter)->energy();
                    if(iter == superCluster->clustersBegin()) elecEe += energy;
                    else elecEgamma += energy;
                }
            }
        } else {
            elecEe = elecEgamma = tau_tuple::DefaultFillValue<float>();
        }
    }

    // Copied from https://github.com/cms-tau-pog/TauAnalysisTools/blob/master/TauAnalysisTools/plugins/TauIdMVATrainingNtupleProducer.cc#L808-L833
    static const reco::GenParticle* FindMatchingGenParticle(const reco::Candidate::LorentzVector& recTauP4,
        const reco::GenParticleCollection& genParticles, double minGenVisPt, const std::vector<int>& pdgIds,
        double dRmatch, double& dRmin)
    {
        const reco::GenParticle* genParticle_matched = nullptr;
        dRmin = dRmatch;
        for ( reco::GenParticleCollection::const_iterator genParticle = genParticles.begin();
              genParticle != genParticles.end(); ++genParticle ) {
            if ( !(genParticle->pt() > minGenVisPt) ) continue;
            double dR = ROOT::Math::VectorUtil::DeltaR(genParticle->p4(), recTauP4);
            if ( dR < dRmin ) {
                bool matchedPdgId = false;
                for ( std::vector<int>::const_iterator pdgId = pdgIds.begin(); pdgId != pdgIds.end(); ++pdgId ) {
                    if ( genParticle->pdgId() == (*pdgId) ) {
                        matchedPdgId = true;
                        break;
                    }
                }
                if ( matchedPdgId ) {
                    genParticle_matched = &(*genParticle);
                    dRmin = dR;
                }
            }
        }
        return genParticle_matched;
    }

private:
    const bool isMC, saveGenTopInfo;

    edm::EDGetTokenT<GenEventInfoProduct> genEvent_token;
    edm::EDGetTokenT<TtGenEvent> topGenEvent_token;
    edm::EDGetTokenT<std::vector<reco::GenParticle>> genParticles_token;
    edm::EDGetTokenT<std::vector<PileupSummaryInfo>> puInfo_token;
    edm::EDGetTokenT<std::vector<reco::Vertex>> vertices_token;
    edm::EDGetTokenT<double> rho_token;
    edm::EDGetTokenT<pat::ElectronCollection> electrons_token;
    edm::EDGetTokenT<pat::MuonCollection> muons_token;
    edm::EDGetTokenT<pat::TauCollection> taus_token;

    tau_tuple::TauTuple tauTuple;
    TauIdMVAAuxiliaries clusterVariables;
};

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(TauTupleProducer);