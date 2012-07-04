#include "time_dependent.h"
#include "type/data.h"
#include "utils/timer.h"
//#include "valgrind/callgrind.h"
using namespace navitia;


void benchmark(routing::TimeDependent & td, type::Data & data){
    {
    Timer t("Calcul d'itinéraire");
    for(int i=0; i < 100; ++i){
        routing::Path res1;
        std::vector<routing::PathItem> res2;
        std::cout << "Depart de " << data.pt_data.stop_areas[i].name << std::endl;
        {Timer t2("simple");
            std::cout << "  ";
            res1 = td.compute(data.pt_data.stop_areas[i].idx, data.pt_data.stop_areas[1142].idx, 8000, 8);
        }
        {Timer t3("astar");
            std::cout << "  ";
            res2 = td.compute_astar(data.pt_data.stop_areas[i], data.pt_data.stop_areas[1142], 8000, 8);
        }
/*        if(res1.items != res2){
            std::cout << "DIJKSTRA RESULT " << std::endl;
            for(auto x: res1.items) std::cout << x << std::endl;
            std::cout << "A* RESULT" << std::endl;
            for(auto x: res2) std::cout << x << std::endl;
        }*/
    }
     }
}

int main(int, char**){
    type::Data data;
    {
        Timer t("Chargement des données");
        data.load_lz4("IdF.nav");
        std::cout << "Num RoutePoints : " << data.pt_data.route_points.size() << std::endl;
        int count = 0;
        BOOST_FOREACH(auto sp, data.pt_data.stop_points){
            if(sp.route_point_list.size() <= 1)
                count++;
        }
        std::cout << count << " stop points avec un seul route point sur " << data.pt_data.stop_points.size() << std::endl;
    }

    routing::TimeDependent td(data.pt_data);
    {
        Timer t("Constuction du graphe");
        td.build_graph();
        td.build_heuristic(data.pt_data.stop_areas[1142].idx + data.pt_data.route_points.size());
        std::cout << "Num nodes: " <<  boost::num_vertices(td.graph) << ", num edges: " << boost::num_edges(td.graph) << std::endl;
    }


    {
    //    Timer t("Calcul d'itinéraire");

/*
        // Pont de neuilly : 13135
        // Gallieni 5980
        // Porte maillot 13207
        // bérault 1878
        // nation 12498
        // buzenval 1142
        bool b;
        routing::edge_t e;

        boost::tie(e, b) = boost::edge(188175, 188176, td.graph);
        BOOST_ASSERT(b);
        std::cout << td.graph[e].t.time_table.size() << std::endl;
        routing::DateTime start_time;
        start_time.hour = 3600*8;
        start_time.date = 8;

        auto res = td.graph[e].t.eval(start_time, td.data);
        std::cout << start_time << " " << res << std::endl;

        std::set<type::idx_t> moo;
        for(auto x : td.graph[e].t.time_table){
            if(x.first.hour > start_time.hour){
                moo.insert(x.first.vp_idx);
            }
        }
        for(auto x : moo ){
            std::cout << x << " " << data.pt_data.validity_patterns[x].days << std::endl;
        }


        boost::tie(e, b) = boost::edge(188213, 188214, td.graph);
        BOOST_ASSERT(e);
        std::cout << td.graph[e].t.time_table.size() << std::endl;

        boost::tie(e, b) = boost::edge(188282, 188283, td.graph);
        BOOST_ASSERT(e);
        std::cout << td.graph[e].t.time_table.size() << std::endl;
*/

        /*auto l = td.compute(data.pt_data.stop_areas[1142], data.pt_data.stop_areas[12498], 8000, 8);
        for(auto s : l){
            std::cout << s.stop_point_name << std::endl;
        }

        l = td.compute(data.pt_data.stop_areas[12498], data.pt_data.stop_areas[1142], 8000, 8);
        for(auto s : l){
            std::cout << s.stop_point_name << " " << s.time << " " << s.day << std::endl;
        }*/

//        CALLGRIND_START_INSTRUMENTATION;
        benchmark(td, data);
//        CALLGRIND_STOP_INSTRUMENTATION;
    }
}
