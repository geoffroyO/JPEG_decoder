#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>


#include "jpeg_reader.h"
#include "huffman.h"
#include "bitstream.h"
#include "structure.h"
#include "decode_bloc.h"
#include "zig_zag.h"
#include "inverse_quantization.h"
#include "idct.h"
#include "conversion_couleurs.h"
#include "conversion_ppm.h"
#include "upsampling.h"



void etire_droite(uint8_t composante_a_etirer[64], uint8_t composante_1[64], uint8_t composante_2[64])
    /* Ici on étire le bloc vers la droite
     * les pixels sont dupliqués:
     * 11223344\\\55667788 */
{
    uint8_t k_comp_1 = 0; //indice de déplacement dans la premiere nouvelle composante
    uint8_t k_comp_2 = 0; //indice de déplacement dans la seconde nouvelle composante

    for (int i = 0; i < 8; i++) {

        for (int j = 0; j < 8; j++) {
            uint8_t pixel = composante_a_etirer[8*i + j]; //pixel[i, j]

            if (j < 4) {
                composante_1[k_comp_1] = pixel;
                composante_1[k_comp_1 + 1] = pixel; //on duplique pixel simplement
                k_comp_1 += 2;
            }

            else {
                composante_2[k_comp_2] = pixel;
                composante_2[k_comp_2 + 1] = pixel; //on duplique pixel simplement
                k_comp_2 += 2;
            }
        }
    }
}

void etire_bas(uint8_t *composante_a_etirer, uint8_t *composante_1, uint8_t *composante_2)
    /* Ici on étire le bloc vers le bas
     * les lignes sont dupliquées:
     * 11223344\\\55667788 */
{
    uint8_t k = 0; //indice de déplacement
    for (int i = 0; i < 8; i++) {

        if (i == 4) { //on remet l'indice de déplacement à 0 car on passe à la deuxième composante
            k = 0;
        }

        for (int j = 0; j < 8; j++) {
            uint8_t pixel = composante_a_etirer[8*i + j]; //pixel[i,j]

            if (i < 4) {
                composante_1[k] = pixel;
                composante_1[k + 8] = pixel; //on duplique pixel à la ligne suivante
                k += 1;
            }

            else {
                composante_2[k] = pixel;
                composante_2[k + 8] = pixel; //on duplique pixel à la ligne suivante
                k += 1;
            }
        }
        k += 8;
    }
}

void copier_idct2tab(struct MCU *p_mcu)
    /* On copie les valeurs de idct dans tab pas très efficace
     * car on ne tient pas compte de l'upsampling fait précédement */
 {
    for (int k = 0; k < 4; k++) {
        for (int j = 0; j < 64; j++) {
            p_mcu->tabY[k][j] = p_mcu->idctY[k][j];
            p_mcu->tabCb[k][j] = p_mcu->idctCb[k][j];
            p_mcu->tabCr[k][j] = p_mcu->idctCr[k][j];
        }
    }
}

void upsampling_MCU(struct MCU *mcu, struct jpeg_desc *jpeg)
    /* Dans cette fonction, on
     * procède à l'étape d'upsampling pour h,v <=4
     * de la mcu on commence par Cb puis Cr
     * on peut réduire assez facilement le nombre de
     * lignes (regarder les mcu->Cb/Cr) */
{
    copier_idct2tab(mcu);

    uint8_t h_Y = get_frame_component_sampling_factor(jpeg, DIR_H, 0);
    uint8_t v_Y = get_frame_component_sampling_factor(jpeg, DIR_V, 0);

    //upsampling de Cb
    uint8_t h_Cb = get_frame_component_sampling_factor(jpeg, DIR_H, 1);
    uint8_t v_Cb = get_frame_component_sampling_factor(jpeg, DIR_V, 1);


    if (h_Y != 1 || v_Y != 1){ //ici on prend en compte le noir et blanc en même temps

            if (h_Cb != 2 || v_Cb != 2) { //si on rentre pas dans le if indirectement on sait h_Y=v_y=2

                if (h_Cb == 2 && v_Cb == 1 && h_Y != 2 && v_Y != 1) { //sinon pas besoin d'upsampling --> nécessairement h_Y=v_Y=2
                    uint8_t Cb0[64];
                    copier_bloc(Cb0, (mcu->tabCb)[0]);
                    uint8_t Cb1[64];
                    copier_bloc(Cb1, (mcu->tabCb)[1]);
                    etire_bas(Cb0, (mcu->tabCb)[0], (mcu->tabCb)[2]);
                    etire_bas(Cb1, (mcu->tabCb)[1], (mcu->tabCb)[3]);
                }

                else if (h_Cb == 1 && v_Cb == 2 && v_Y != 2 && h_Y != 1) { //sinon pas besoin d'upsampling --> nécessairement h_Y=v_Y=2
                    uint8_t Cb0[64];
                    copier_bloc(Cb0, (mcu->tabCb)[0]);
                    uint8_t Cb1[64];
                    copier_bloc(Cb1, (mcu->tabCb)[1]);
                    etire_droite(Cb0, (mcu->tabCb)[0], (mcu->tabCb)[1]);
                    etire_droite(Cb1, (mcu->tabCb)[2], (mcu->tabCb)[3]);
                }

                else if (h_Cb == 1 && v_Cb == 1) { //on sait déjà h_Y != 1 et v_Y != 1
                    if (h_Y == 2 && v_Y == 2) {
                        uint8_t Cb0[64];
                        copier_bloc(Cb0, (mcu->tabCb)[0]);
                        uint8_t Cb1[64];
                        etire_droite(Cb0, (mcu->tabCb)[0], (mcu->tabCb)[1]); //etirage à droite d'abord ou bas d'abord ??
                        copier_bloc(Cb1, (mcu->tabCb)[1]);
                        copier_bloc(Cb0, (mcu->tabCb)[0]);
                        etire_bas(Cb0, (mcu->tabCb)[0], (mcu->tabCb)[2]);
                        etire_bas(Cb1, (mcu->tabCb)[1], (mcu->tabCb)[3]);

                    }

                    else if (h_Y == 2 && v_Y == 1) {
                        uint8_t Cb0[64];
                        copier_bloc(Cb0, (mcu->tabCb)[0]);
                        etire_droite(Cb0, (mcu->tabCb)[0], (mcu->tabCb)[1]);
                    }

                    else if (h_Y == 1 && v_Y == 2){
                        uint8_t Cb0[64];
                        copier_bloc(Cb0, (mcu->tabCb)[0]);
                        etire_bas(Cb0, (mcu->tabCb)[0], (mcu->tabCb)[1]);
                    }

                }
            }
    }

    //upsampling de Cr
    uint8_t h_Cr = get_frame_component_sampling_factor(jpeg, DIR_H, 2);
    uint8_t v_Cr = get_frame_component_sampling_factor(jpeg, DIR_V, 2);

    if (h_Y != 1 || v_Y != 1){ //ici on prend en compte le noir et blanc en même temps

            if (h_Cr != 2 || v_Cr != 2) { //si on rentre pas dans le if indirectement on sait h_Y=v_y=2

                if (h_Cr == 2 && v_Cr == 1 && h_Y != 2 && v_Y != 1) { //sinon pas besoin d'upsampling --> nécessairement h_Y=v_Y=2
                    uint8_t Cr0[64];
                    copier_bloc(Cr0, (mcu->tabCr)[0]);
                    uint8_t Cr1[64];
                    copier_bloc(Cr1, (mcu->tabCr)[1]);
                    etire_bas(Cr0, (mcu->tabCr)[0], (mcu->tabCr)[2]);
                    etire_bas(Cr1, (mcu->tabCr)[1], (mcu->tabCr)[3]);
                }

                else if (h_Cr == 1 && v_Cr == 2 && v_Y != 2 && h_Y != 1) { //sinon pas besoin d'upsampling --> nécessairement h_Y=v_Y=2
                    uint8_t Cr0[64];
                    copier_bloc(Cr0, (mcu->tabCr)[0]);
                    uint8_t Cr1[64];
                    copier_bloc(Cr1, (mcu->tabCr)[1]);
                    etire_droite(Cr0, (mcu->tabCr)[0], (mcu->tabCr)[1]);
                    etire_droite(Cr1, (mcu->tabCr)[2], (mcu->tabCr)[3]);
                }

                else if (h_Cr == 1 && v_Cr == 1) { //on sait déjà h_Y != 1 et v_Y != 1
                    if (h_Y == 2 && v_Y == 2) {
                        uint8_t Cr0[64];
                        copier_bloc(Cr0, (mcu->tabCr)[0]);
                        uint8_t Cr1[64];
                        etire_droite(Cr0, (mcu->tabCr)[0], (mcu->tabCr)[1]); //etirage à droite d'abord ou bas d'abord ??
                        copier_bloc(Cr1, (mcu->tabCr)[1]);
                        copier_bloc(Cr0, (mcu->tabCr)[0]);
                        etire_bas(Cr0, (mcu->tabCr)[0], (mcu->tabCr)[2]);
                        etire_bas(Cr1, (mcu->tabCr)[1], (mcu->tabCr)[3]);
                    }

                    else if (h_Y == 2 && v_Y == 1) {
                        uint8_t Cr0[64];
                        copier_bloc(Cr0, (mcu->tabCr)[0]);
                        etire_droite(Cr0, (mcu->tabCr)[0], (mcu->tabCr)[1]);
                    }

                    else if (h_Y == 1 && v_Y == 2){
                        uint8_t Cr0[64];
                        copier_bloc(Cr0, (mcu->tabCr)[0]);
                        etire_bas(Cr0, (mcu->tabCr)[0], (mcu->tabCr)[1]);
                    }

                }
            }
    }
}

void afficher_blocu8(uint8_t *bloc)
{
    for (int i=0; i<64; i++){

        printf("%hhx, ", bloc[i]);
    }
    printf("\n");
}



