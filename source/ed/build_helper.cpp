/* Copyright © 2001-2014, Canal TP and/or its affiliates. All rights reserved.
  
This file is part of Navitia,
    the software to build cool stuff with public transport.
 
Hope you'll enjoy and contribute to this project,
    powered by Canal TP (www.canaltp.fr).
Help us simplify mobility and open public transport:
    a non ending quest to the responsive locomotion way of traveling!
  
LICENCE: This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
   
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Affero General Public License for more details.
   
You should have received a copy of the GNU Affero General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
  
Stay tuned using
twitter @navitia 
IRC #navitia on freenode
https://groups.google.com/d/forum/navitia
www.navitia.io
*/

#include "build_helper.h"
#include "ed/connectors/gtfs_parser.h"
#include <boost/range/algorithm/find_if.hpp>

namespace pt = boost::posix_time;
namespace dis = nt::disruption;

namespace ed {

VJ::VJ(builder & b, const std::string &line_name, const std::string &validity_pattern,
       bool is_frequency,
       bool wheelchair_boarding, const std::string& uri,
       const std::string& meta_vj_name, const std::string& physical_mode): b(b) {
    // the vj is owned by the route, so we need to have the route before everything else
    auto it = b.lines.find(line_name);
    nt::Route* route = nullptr;
    nt::PT_Data& pt_data = *(b.data->pt_data);
    if (it == b.lines.end()) {
        navitia::type::Line* line = new navitia::type::Line();
        line->idx = pt_data.lines.size();
        line->uri = line_name;
        b.lines[line_name] = line;
        line->name = line_name;
        pt_data.lines.push_back(line);

        route = new navitia::type::Route();
        route->idx = pt_data.routes.size();
        route->name = line->name;
        route->uri = line_name + ":" + std::to_string(pt_data.routes.size());
        pt_data.routes.push_back(route);
        line->route_list.push_back(route);
        route->line = line;
        pt_data.routes_map[route->uri] = route;
    } else {
        route = it->second->route_list.front();
    }

    std::string name;
    if (! meta_vj_name.empty()) {
        name = meta_vj_name;
    } else if (! uri.empty()) {
        name = uri;
    } else {
        auto idx = pt_data.vehicle_journeys.size();
        name = "vehicle_journey " + std::to_string(idx);
    }
    // NOTE: the meta vj name should be the same as the vj's name
    nt::MetaVehicleJourney* mvj = pt_data.meta_vjs.get_or_create(name);

    if (is_frequency) {
        auto f_vj = mvj->create_frequency_vj();
        route->frequency_vehicle_journey_list.push_back(f_vj);
        vj = f_vj;
    } else {
        auto d_vj = mvj->create_discrete_vj();
        route->discrete_vehicle_journey_list.push_back(d_vj);
        vj = d_vj;
    }
    vj->route = route;

    //add physical mode
    if (!physical_mode.empty()) {
        auto it = pt_data.physical_modes_map.find(physical_mode);
        if (it != pt_data.physical_modes_map.end()){
            vj->physical_mode = it->second;
        }
    }
    if (!vj->physical_mode) {
        if (physical_mode.empty() && pt_data.physical_modes.size()){
            vj->physical_mode = pt_data.physical_modes.front();
        } else {
            const auto name = physical_mode.empty() ? "physical_mode:0" : physical_mode;
            auto* physical_mode = new navitia::type::PhysicalMode();
            physical_mode->idx = pt_data.physical_modes.size();
            physical_mode->uri = name;
            physical_mode->name = "name " + name;
            pt_data.physical_modes.push_back(physical_mode);
            vj->physical_mode = physical_mode;
        }
    }

    vj->physical_mode->vehicle_journey_list.push_back(vj);

    vj->idx = pt_data.vehicle_journeys.size();
    if (! uri.empty()) {
        vj->uri = uri;
    } else {
        vj->uri = "vj:" + line_name + ":" + std::to_string(vj->idx);
    }


    pt_data.headsign_handler.change_name_and_register_as_headsign(*vj, name);

    pt_data.vehicle_journeys.push_back(vj);
    pt_data.vehicle_journeys_map[vj->uri] = vj;

    nt::ValidityPattern* vp = new nt::ValidityPattern(b.begin, validity_pattern);
    auto find_vp_predicate = [&](nt::ValidityPattern* vp1) { return vp->days == vp1->days;};
    auto it_vp = std::find_if(pt_data.validity_patterns.begin(),
            pt_data.validity_patterns.end(), find_vp_predicate);
    if (it_vp != pt_data.validity_patterns.end()) {
        delete vp;
        vp = *(it_vp);
    } else {
        pt_data.validity_patterns.push_back(vp);
    }
    //by default we assign all the validity patterns (base/adapted/realtime) to the same vp
    for (const auto& vj_vp: vj->validity_patterns) {
        vj_vp.second = vp;
    }

    if (wheelchair_boarding) {
        vj->set_vehicle(navitia::type::hasVehicleProperties::WHEELCHAIR_ACCESSIBLE);
    }

    if (! pt_data.companies.empty()) {
        vj->company = pt_data.companies.front();
    }
}

VJ& VJ::st_shape(const navitia::type::LineString& shape) {
    assert(shape.size() >= 2);
    assert(vj->stop_time_list.size() >= 2);
    assert(vj->stop_time_list.back().stop_point->coord == shape.back());
    assert(vj->stop_time_list.at(vj->stop_time_list.size() - 2).stop_point->coord
           == shape.front());
    vj->stop_time_list.back().shape_from_prev = b.data->pt_data->shape_manager.get(shape);
    return *this;
}

VJ& VJ::operator()(const std::string &stopPoint,const std::string& arrivee, const std::string& depart,
            uint16_t local_traffic_zone, bool drop_off_allowed, bool pick_up_allowed){
    return (*this)(stopPoint, pt::duration_from_string(arrivee).total_seconds(),
            pt::duration_from_string(depart).total_seconds(), local_traffic_zone,
            drop_off_allowed, pick_up_allowed);
}

VJ & VJ::operator()(const std::string & sp_name, int arrivee, int depart, uint16_t local_trafic_zone,
                    bool drop_off_allowed, bool pick_up_allowed){
    auto it = b.sps.find(sp_name);
    navitia::type::StopPoint* sp = nullptr;
    if(it == b.sps.end()){
        sp = new navitia::type::StopPoint();
        sp->idx = b.data->pt_data->stop_points.size();
        sp->name = sp_name;
        sp->uri = sp_name;
        if(!b.data->pt_data->networks.empty())
            sp->network = b.data->pt_data->networks.front();

        b.sps[sp_name] = sp;
        b.data->pt_data->stop_points.push_back(sp);
        auto sa_it = b.sas.find(sp_name);
        if(sa_it == b.sas.end()) {
            navitia::type::StopArea* sa = new navitia::type::StopArea();
            sa->idx = b.data->pt_data->stop_areas.size();
            sa->name = sp_name;
            sa->uri = sp_name;
            sa->set_property(navitia::type::hasProperties::WHEELCHAIR_BOARDING);
            sp->stop_area = sa;
            b.sas[sp_name] = sa;
            b.data->pt_data->stop_areas.push_back(sa);
            sp->set_properties(sa->properties());
            sa->stop_point_list.push_back(sp);
        } else {
            sp->stop_area = sa_it->second;
            sp->coord.set_lat(sp->stop_area->coord.lat());
            sp->coord.set_lon(sp->stop_area->coord.lon());
            sp->set_properties(sa_it->second->properties());
            sa_it->second->stop_point_list.push_back(sp);
        }
    } else {
        sp = it->second;
    }

    navitia::type::StopTime st;
    st.stop_point = sp;

    if(depart == -1) depart = arrivee;
    st.arrival_time = arrivee;
    st.departure_time = depart;
    st.vehicle_journey = vj;
    st.local_traffic_zone = local_trafic_zone;
    st.set_drop_off_allowed(drop_off_allowed);
    st.set_pick_up_allowed(pick_up_allowed);

    vj->stop_time_list.push_back(st);
    return *this;
}

SA::SA(builder & b, const std::string & sa_name, double x, double y,
       bool create_sp, bool wheelchair_boarding)
       : b(b) {
    sa = new navitia::type::StopArea();
    sa->idx = b.data->pt_data->stop_areas.size();
    b.data->pt_data->stop_areas.push_back(sa);
    sa->name = sa_name;
    sa->uri = sa_name;
    sa->coord.set_lon(x);
    sa->coord.set_lat(y);
    if(wheelchair_boarding)
        sa->set_property(types::hasProperties::WHEELCHAIR_BOARDING);
    b.sas[sa_name] = sa;

    if (create_sp) {
        auto sp = new navitia::type::StopPoint();
        sp->idx = b.data->pt_data->stop_points.size();
        b.data->pt_data->stop_points.push_back(sp);
        sp->name = "stop_point:"+ sa_name;
        sp->uri = sp->name;
        if(wheelchair_boarding)
            sp->set_property(navitia::type::hasProperties::WHEELCHAIR_BOARDING);
        sp->coord.set_lon(x);
        sp->coord.set_lat(y);

        sp->stop_area = sa;
        b.sps[sp->name] = sp;
        sa->stop_point_list.push_back(sp);
    }
}

SA & SA::operator()(const std::string & sp_name, double x, double y, bool wheelchair_boarding){
    navitia::type::StopPoint * sp = new navitia::type::StopPoint();
    sp->idx = b.data->pt_data->stop_points.size();
    b.data->pt_data->stop_points.push_back(sp);
    sp->name = sp_name;
    sp->uri = sp_name;
    if(wheelchair_boarding)
        sp->set_property(navitia::type::hasProperties::WHEELCHAIR_BOARDING);
    sp->coord.set_lon(x);
    sp->coord.set_lat(y);

    sp->stop_area = this->sa;
    this->sa->stop_point_list.push_back(sp);
    b.sps[sp_name] = sp;
    return *this;
}

DisruptionCreator::DisruptionCreator(builder& b, const std::string& uri, nt::RTLevel lvl):
    b(b), disruption(b.data->pt_data->disruption_holder.make_disruption(uri, lvl)) {}

Impacter& DisruptionCreator::impact() {
    impacters.emplace_back(b, disruption);
    return impacters.back();
}

Impacter::Impacter(builder& bu, dis::Disruption& disrup): b(bu) {
    impact = boost::make_shared<dis::Impact>();

    disrup.add_impact(impact);
}

DisruptionCreator& DisruptionCreator::tag(const std::string& t) {
    auto tag = boost::make_shared<dis::Tag>();
    tag->uri = t;
    tag->name = t + " name";
    disruption.tags.push_back(tag);
    return *this;
}

DisruptionCreator builder::disrupt(nt::RTLevel lvl, const std::string& uri) {
    return DisruptionCreator(*this, uri, lvl);
}

std::string get_random_id() {
    boost::uuids::uuid uuid = boost::uuids::random_generator()();
    std::stringstream uuid_stream;
    uuid_stream << uuid;
    return uuid_stream.str();
}

Impacter& Impacter::severity(dis::Effect e,
                             std::string uri,
                             const std::string& wording,
                             const std::string& color,
                             int priority) {
    if (uri.empty()) {
        // we get the effect
        uri = to_string(e);
    }
    auto& sev_map = b.data->pt_data->disruption_holder.severities;
    auto it = sev_map.find(uri);
    if (it != std::end(sev_map)) {
        impact->severity = it->second.lock();
        return *this;
    }
    auto severity = boost::make_shared<dis::Severity>();
    severity->uri = uri;
    if (! wording.empty()) {
        severity->wording = wording;
    } else {
        severity->wording = uri + " severity";
    }
    severity->color = color;
    severity->priority = priority;
    severity->effect = e;
    sev_map[severity->uri] = severity;
    impact->severity = severity;
    return *this;
}

Impacter& Impacter::severity(const std::string& uri) {
    auto& sev_map = b.data->pt_data->disruption_holder.severities;
    auto it = sev_map.find(uri);
    if (it == std::end(sev_map)) {
        throw navitia::exception("unknown severity " + uri + ", create it first");
    }
    impact->severity = it->second.lock();
    return *this;
}

Impacter& Impacter::informed_entities(nt::Type_e type, const std::string& uri) {
    impact->informed_entities.push_back(dis::make_pt_obj(type, uri, *b.data->pt_data, impact));
    return *this;
}

Impacter& Impacter::on(nt::Type_e type, const std::string& uri) {
    return informed_entities(type, uri);
}

Impacter& Impacter::msg(const dis::Message& m) {
    impact->messages.push_back(m);
    return *this;
}

Impacter& Impacter::msg(const std::string& text, nt::disruption::ChannelType c) {
    dis::Message m;
    auto str = to_string(c);
    m.text = text;
    m.channel_id = str;
    m.channel_id = str;
    m.channel_name = str + " channel";
    m.channel_content_type = "content type";
    m.created_at = boost::posix_time::ptime(b.data->meta->production_date.begin(),
                                            boost::posix_time::minutes(0));

    m.channel_types.insert(c);
    return msg(m);
}

/*
 * helper to create a disruption with only one impact
 */
Impacter builder::impact(nt::RTLevel lvl, std::string disruption_uri) {
    if (disruption_uri.empty()) {
        disruption_uri = get_random_id();
    }
    auto& disruption = data->pt_data->disruption_holder.make_disruption(disruption_uri, lvl);
    return Impacter(*this, disruption);
}

VJ builder::vj(const std::string& line_name,
               const std::string& validity_pattern,
               const std::string& block_id,
               const bool wheelchair_boarding,
               const std::string& uri,
               const std::string& meta_vj,
               const std::string& physical_mode){
    return vj_with_network("base_network", line_name, validity_pattern, block_id,
                           wheelchair_boarding, uri, meta_vj, physical_mode);
}

VJ builder::vj_with_network(const std::string& network_name,
               const std::string& line_name,
               const std::string& validity_pattern,
               const std::string& block_id,
               const bool wheelchair_boarding,
               const std::string& uri,
               const std::string& meta_vj,
               const std::string& physical_mode,
               bool is_frequency) {
    auto res = VJ(*this, line_name, validity_pattern, is_frequency,
                  wheelchair_boarding, uri, meta_vj, physical_mode);
    auto vj = this->data->pt_data->vehicle_journeys.back();
    auto it = this->nts.find(network_name);
    if(it == this->nts.end()){
        navitia::type::Network* network = new navitia::type::Network();
        network->idx = this->data->pt_data->networks.size();
        network->uri = network_name;
        network->name = network_name;
        this->nts[network_name] = network;
        this->data->pt_data->networks.push_back(network);
        vj->route->line->network = network;
        network->line_list.push_back(vj->route->line);
    } else {
        vj->route->line->network = it->second;

        const auto* line = vj->route->line;
        if (boost::find_if(it->second->line_list, [&](navitia::type::Line* l) { return l->uri == line->uri; })
            == it->second->line_list.end()) {
            it->second->line_list.push_back(vj->route->line);
        }
    }
    if(block_id != "") {
        block_vjs.insert(std::make_pair(block_id, vj));
    }
    return res;
}


VJ builder::frequency_vj(const std::string& line_name,
                uint32_t start_time,
                uint32_t end_time,
                uint32_t headway_secs,
                const std::string& network_name,
                const std::string& validity_pattern,
                const std::string& block_id,
                const bool wheelchair_boarding,
                const std::string& uri,
                const std::string& meta_vj){
    auto res = vj_with_network(network_name, line_name, validity_pattern, block_id,
                               wheelchair_boarding, uri, meta_vj, "", true);
    auto vj = static_cast<nt::FrequencyVehicleJourney*>(this->data->pt_data->vehicle_journeys.back());

    //we get the last frequency vj of the jp, it's the one we just created
    vj->start_time = start_time;

    size_t nb_trips = std::ceil((end_time - start_time) / headway_secs);
    vj->end_time = start_time + ( nb_trips * headway_secs );
    vj->headway_secs = headway_secs;

    return res;
}


SA builder::sa(const std::string &name, double x, double y,
               const bool create_sp, const bool wheelchair_boarding) {
    return SA(*this, name, x, y, create_sp, wheelchair_boarding);
}


void builder::connection(const std::string & name1, const std::string & name2, float length) {
    navitia::type::StopPointConnection* connexion = new navitia::type::StopPointConnection();
    connexion->idx = data->pt_data->stop_point_connections.size();
    if(sps.count(name1) == 0 || sps.count(name2) == 0)
        return ;
    connexion->departure = (*(sps.find(name1))).second;
    connexion->destination = (*(sps.find(name2))).second;

//connexion->connection_kind = types::ConnectionType::Walking;
    connexion->duration = length;
    connexion->display_duration = length;

    data->pt_data->stop_point_connections.push_back(connexion);
    connexion->departure->stop_point_connection_list.push_back(connexion);
    connexion->destination->stop_point_connection_list.push_back(connexion);
}

 static double get_co2_emission(const std::string& uri){
     if (uri == "0x0"){
         return 4.;
     }
     if (uri == "Bss"){
         return 0.;
     }
     if (uri == "Bike"){
         return 0.;
     }
     if (uri == "Bus"){
         return 132.;
     }
     if (uri == "Car"){
         return 184.;
     }
    return -1.;
 }

 void builder::generate_dummy_basis() {
    navitia::type::Company *company = new navitia::type::Company();
    company->idx = this->data->pt_data->companies.size();
    company->name = "base_company";
    company->uri = "base_company";
    this->data->pt_data->companies.push_back(company);

    const std::string default_network_name = "base_network";
    if (this->nts.find(default_network_name) == this->nts.end()) {
        navitia::type::Network *network = new navitia::type::Network();
        network->idx = this->data->pt_data->networks.size();
        network->name = default_network_name;
        network->uri = default_network_name;
        this->data->pt_data->networks.push_back(network);
        this->nts.insert({network->uri, network});
    }

    navitia::type::CommercialMode *commercial_mode = new navitia::type::CommercialMode();
    commercial_mode->idx = this->data->pt_data->commercial_modes.size();
    commercial_mode->name = "Tram";
    commercial_mode->uri = "0x0";
    this->data->pt_data->commercial_modes.push_back(commercial_mode);

    commercial_mode = new navitia::type::CommercialMode();
    commercial_mode->idx = this->data->pt_data->commercial_modes.size();
    commercial_mode->name = "Metro";
    commercial_mode->uri = "0x1";
    this->data->pt_data->commercial_modes.push_back(commercial_mode);

    commercial_mode = new navitia::type::CommercialMode();
    commercial_mode->idx = this->data->pt_data->commercial_modes.size();
    commercial_mode->name = "Bss";
    commercial_mode->uri = "Bss";
    this->data->pt_data->commercial_modes.push_back(commercial_mode);

    commercial_mode = new navitia::type::CommercialMode();
    commercial_mode->idx = this->data->pt_data->commercial_modes.size();
    commercial_mode->name = "Bike";
    commercial_mode->uri = "Bike";
    this->data->pt_data->commercial_modes.push_back(commercial_mode);

    commercial_mode = new navitia::type::CommercialMode();
    commercial_mode->idx = this->data->pt_data->commercial_modes.size();
    commercial_mode->name = "Bus";
    commercial_mode->uri = "Bus";
    this->data->pt_data->commercial_modes.push_back(commercial_mode);

    commercial_mode = new navitia::type::CommercialMode();
    commercial_mode->idx = this->data->pt_data->commercial_modes.size();
    commercial_mode->name = "Car";
    commercial_mode->uri = "Car";
    this->data->pt_data->commercial_modes.push_back(commercial_mode);

    for(navitia::type::CommercialMode *mt : this->data->pt_data->commercial_modes) {
        navitia::type::PhysicalMode* mode = new navitia::type::PhysicalMode();
        double co2_emission;
        mode->idx = mt->idx;
        mode->name = mt->name;
        mode->uri = "physical_mode:" + mt->uri;
        co2_emission = get_co2_emission(mt->uri);
        if (co2_emission >=0.){
            mode->co2_emission = co2_emission;
        }
        this->data->pt_data->physical_modes.push_back(mode);
        this->data->pt_data->physical_modes_map[mode->uri] = mode;
    }
 }

void builder::build_blocks() {
    using navitia::type::VehicleJourney;

    std::map<std::string, std::vector<VehicleJourney*>> block_map;
    for (const auto& block_vj: block_vjs) {
        if (block_vj.first.empty()) { continue; }
        block_map[block_vj.first].push_back(block_vj.second);
    }

    for (auto& item: block_map) {
        auto& vehicle_journeys = item.second;
        std::sort(vehicle_journeys.begin(), vehicle_journeys.end(),
                  [](const VehicleJourney* vj1, const VehicleJourney* vj2) {
                      return vj1->stop_time_list.back().departure_time <=
                          vj2->stop_time_list.front().departure_time;
                  }
            );

        VehicleJourney* prev_vj = nullptr;
        for (auto* vj: vehicle_journeys) {
            if (prev_vj) {
                assert(prev_vj->stop_time_list.back().arrival_time
                       <= vj->stop_time_list.front().departure_time);
                prev_vj->next_vj = vj;
                vj->prev_vj = prev_vj;
            }
            prev_vj = vj;
        }
    }
}

 void builder::finish() {

     build_blocks();
     for(navitia::type::VehicleJourney* vj : this->data->pt_data->vehicle_journeys) {
         if(!vj->prev_vj) {
             vj->stop_time_list.front().set_drop_off_allowed(false);
         }
         if(!vj->next_vj) {
            vj->stop_time_list.back().set_pick_up_allowed(false);
         }
     }

     for (auto* route: data->pt_data->routes) {
         for (auto& freq_vj: route->frequency_vehicle_journey_list) {

             const auto start = freq_vj->stop_time_list.front().arrival_time;
             for (auto& st: freq_vj->stop_time_list) {
                 st.set_is_frequency(true); //we need to tag the stop time as a frequency stop time
                 st.arrival_time -= start;
                 st.departure_time -= start;
             }
         }
     }
     data->build_raptor();
 }

/*
1. Initilise the first admin in the list to all stop_area and way
2. Used for the autocomplete functional tests.
*/
 void builder::manage_admin() {
     if (!data->geo_ref->admins.empty()) {
         navitia::georef::Admin * admin = data->geo_ref->admins[0];
        for(navitia::type::StopArea* sa : data->pt_data->stop_areas){
            sa->admin_list.clear();
            sa->admin_list.push_back(admin);
        }

        for(navitia::georef::Way * way : data->geo_ref->ways) {
            way->admin_list.clear();
            way->admin_list.push_back(admin);
        }
     }
 }

 void builder::build_autocomplete() {
    data->pt_data->build_autocomplete(*(data->geo_ref));
    data->geo_ref->build_autocomplete_list();
    data->compute_labels();
}
}
