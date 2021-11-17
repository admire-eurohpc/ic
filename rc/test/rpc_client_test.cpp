//
// Created by Alberto Cascajo on 15/11/21.
//


#include <cstdlib>
#include <iostream>
#include "../include/root_connections.h"

using namespace std;

int
main()
{

  /* ****** TEST FOR A CLIENT THAT WAITS FOR CONNECTIONS FROM ROOT CONTROLLER ******* */
  margo_instance_id mid;
  int ret = load_environment_controller(mid);
  if (ret == EXIT_FAILURE) return EXIT_FAILURE;
  cout << "Setting up the environment" << endl;

  ret = register_rpc(mid, 2); //mall out
  if (ret == EXIT_FAILURE)
    cout << "Error registering RPC" << endl;
  else
    cout << "IO Sched client working\n" << endl;

  ret = register_rpc(mid, 3); //mall out
  if (ret == EXIT_FAILURE)
    cout << "Error registering RPC" << endl;
  else
    cout << "ADHOC client working\n" << endl;

  ret = register_rpc(mid, 4); //mall out
  if (ret == EXIT_FAILURE)
    cout << "Error registering RPC" << endl;
  else
    cout << "Monitoring client working\n" << endl;

  ret = register_rpc(mid, 5); //slurm out
  if (ret == EXIT_FAILURE)
    cout << "Error registering RPC" << endl;
  else
    cout << "Malleab client working\n" << endl;

  ret = register_rpc(mid, 6); //mon out
  if (ret == EXIT_FAILURE)
    cout << "Error registering RPC" << endl;
  else
    cout << "Slurm client working\n" << endl;

  /*Finish Margo*/
  margo_wait_for_finalize(mid);

  return EXIT_SUCCESS;
}
